#include "Plugins/OcrEngines/PpOcrOnnxEngine.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Ocr/OcrLanguage.h"
#include "Core/Ocr/PpOcrInput.h"
#include "Core/Ocr/PpOcrPostprocess.h"

#include <QCoreApplication>
#include <QFile>
#include <QStringList>

#include <onnxruntime_c_api.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace GyanisScribe::Plugins
{
    namespace
    {
        // 认不出字的最小图尺寸，与另外两条识别通道取同一道门槛
        constexpr int kMinimumImageEdge = 8;

        // 运行库的文件名、环境变量与取接口表的符号名
        constexpr const char *kRuntimeLibraryFileName      = "onnxruntime.dll";
        constexpr const char *kRuntimeEnvironmentVariable = "GYANISSCRIBE_ONNXRUNTIME_DLL";
        constexpr const char *kRuntimeApiSymbol           = "OrtGetApiBase";

        /**
         * @brief 用运行库给的释放函数销毁一个 ORT 对象
         * @tparam T ORT 的不透明类型
         * @note 释放函数随接口表来，所以句柄要连函数指针一起保存；指针为空时析构不做事，
         *       便于让成员先默认构造、取到资源后再接管
         */
        template<typename T>
        struct OrtDeleter
        {
            using ReleaseFunction = void(ORT_API_CALL *)(T *);

            ReleaseFunction release = nullptr;

            void operator()(T *value) const noexcept
            {
                if (value != nullptr && release != nullptr)
                {
                    release(value);
                }
            }
        };

        template<typename T>
        using OrtHandle = std::unique_ptr<T, OrtDeleter<T>>;

        /**
         * @brief 把一个 OrtStatus 收进所有权句柄
         * @param status 运行库刚返回的状态，可空
         * @param api 接口表，提供对应的释放函数
         * @return 负责释放的句柄
         */
        OrtHandle<OrtStatus> takeStatus(OrtStatus *status, const OrtApi *api)
        {
            return OrtHandle<OrtStatus>(status, OrtDeleter<OrtStatus>{api->ReleaseStatus});
        }

        /**
         * @brief 取走状态里的错误消息并释放状态
         * @param status 状态句柄
         * @param api 接口表
         * @return 成功时返回空串；失败返回运行库给的英文原因
         */
        QString consumeStatus(OrtHandle<OrtStatus> status, const OrtApi *api)
        {
            if (status == nullptr)
            {
                return {};
            }
            return QString::fromUtf8(api->GetErrorMessage(status.get()));
        }

        /**
         * @brief 交还运行库分配的名字串
         * @param api 接口表
         * @param allocator 取名字时用的分配器
         * @param name 名字串，可空
         */
        void freeAllocatorName(const OrtApi *api, OrtAllocator *allocator, char *name)
        {
            if (name == nullptr)
            {
                return;
            }
            (void)OrtHandle<OrtStatus>(api->AllocatorFree(allocator, name), OrtDeleter<OrtStatus>{api->ReleaseStatus});
        }

        Base::Error internalError(std::string message)
        {
            return Base::Error{Base::ErrorCode::InternalError, std::move(message)};
        }

        /**
         * @brief 拼一条带模型文件名与运行库原因的失败信息
         * @param fileName 出错的模型文件名
         * @param detail 运行库给的原因
         * @return InternalError
         */
        Base::Error modelFailure(const char *fileName, const QString &detail)
        {
            return internalError(Base::uiFormat(Base::uiTextUtf8("PP-OCR 模型 {} 出错：{}。模型文件可能损坏或与随包的运行库版本不符，请重新安装本程序"), fileName,
                                               detail.toStdString()));
        }

        /**
         * @brief 按「环境变量、程序目录、系统搜索路径」的顺序加载运行库并取接口表
         * @param library 成功时接受运行库句柄，调用方负责 FreeLibrary
         * @param api 成功时接受接口表
         * @return 失败返回中文原因；成功返回空串
         * @note 环境变量设了就走它一条路，文件不在场直接失败，不回落：用户显式指定的运行库
         *       被悄悄换掉，排查时看到的版本与用户以为的不是同一个
         */
        QString openRuntimeLibrary(HMODULE &library, const OrtApi *&api)
        {
            QString attemptedPath = QString::fromLatin1(kRuntimeLibraryFileName);
            HMODULE loaded        = nullptr;

            const QString fromEnvironment = qEnvironmentVariable(kRuntimeEnvironmentVariable);
            if (!fromEnvironment.isEmpty())
            {
                const std::filesystem::path path(fromEnvironment.toStdWString());
                attemptedPath = fromEnvironment;
                std::error_code error;
                if (!std::filesystem::is_regular_file(path, error))
                {
                    return QString::fromStdString(Base::uiFormat(
                            Base::uiTextUtf8("环境变量 {} 指向的 onnxruntime.dll 不存在：{}。请修正该变量，或删除它改用随包的运行库"),
                            kRuntimeEnvironmentVariable, Base::Paths::toDisplayText(path).toStdString()));
                }
                loaded = LoadLibraryW(path.c_str());
            }
            else
            {
                if (QCoreApplication::instance() != nullptr)
                {
                    const std::filesystem::path bundled = Base::Paths::applicationDirectory() / kRuntimeLibraryFileName;
                    std::error_code             error;
                    if (std::filesystem::is_regular_file(bundled, error))
                    {
                        attemptedPath = Base::Paths::toDisplayText(bundled);
                        loaded        = LoadLibraryW(bundled.c_str());
                    }
                }
                if (loaded == nullptr)
                {
                    attemptedPath = QString::fromLatin1(kRuntimeLibraryFileName);
                    loaded        = LoadLibraryW(L"onnxruntime.dll");
                }
            }

            if (loaded == nullptr)
            {
                return QString::fromStdString(Base::uiFormat(
                        Base::uiTextUtf8("onnxruntime.dll 加载失败（{}，系统错误码 {}）。请重新安装本程序以恢复随包的运行库"), attemptedPath.toStdString(),
                        GetLastError()));
            }

            using GetApiBaseFunction = const OrtApiBase *(ORT_API_CALL *)();
            const auto symbol        = GetProcAddress(loaded, kRuntimeApiSymbol);
            if (symbol == nullptr)
            {
                FreeLibrary(loaded);
                return QString::fromStdString(Base::uiFormat(
                        Base::uiTextUtf8("onnxruntime.dll 里找不到 {}（{}），文件可能被换成了别的版本。请重新安装本程序以恢复随包的运行库"), kRuntimeApiSymbol,
                        attemptedPath.toStdString()));
            }

            // 直接函数指针强转会触发 C4191，经一次字节拷贝换类型，句柄的二进制表示不变
            GetApiBaseFunction getApiBase = nullptr;
            static_assert(sizeof(getApiBase) == sizeof(symbol));
            std::memcpy(&getApiBase, &symbol, sizeof(getApiBase));

            const OrtApiBase *apiBase = getApiBase();
            if (apiBase == nullptr)
            {
                FreeLibrary(loaded);
                return QString::fromStdString(Base::uiFormat(
                        Base::uiTextUtf8("onnxruntime.dll 的接口表为空（{}）。请重新安装本程序以恢复随包的运行库"), attemptedPath.toStdString()));
            }

            const OrtApi *runtimeApi = apiBase->GetApi(ORT_API_VERSION);
            if (runtimeApi == nullptr)
            {
                // 版本串指向 DLL 映像里的静态区：先拷进自己的串再卸载，卸载后原指针随时可能失效
                const char       *versionText = apiBase->GetVersionString();
                const std::string version     = versionText != nullptr ? std::string(versionText) : std::string("未知");
                FreeLibrary(loaded);
                return QString::fromStdString(Base::uiFormat(
                        Base::uiTextUtf8("onnxruntime.dll 的接口版本与程序不符：程序需要 ONNX Runtime API 版本 {}，实际库是 {}。请重新安装本程序以恢复随包的运行库"),
                        ORT_API_VERSION, version));
            }

            library = loaded;
            api     = runtimeApi;
            return {};
        }

        /**
         * @brief 把检测概率图上的框折回原图坐标
         * @param box 概率图坐标系里的框
         * @param tensor 构造检测输入时记下的缩放比例
         * @param imageRect 原图范围
         * @return 夹回原图范围的框；不相交时为空框
         */
        QRect mapBackToImage(const QRect &box, const Core::PpOcrTensor &tensor, const QRect &imageRect)
        {
            const double inverseScaleX = tensor.scaleX > 0.0 ? 1.0 / tensor.scaleX : 1.0;
            const double inverseScaleY = tensor.scaleY > 0.0 ? 1.0 / tensor.scaleY : 1.0;

            const int left   = static_cast<int>(std::lround(static_cast<double>(box.left()) * inverseScaleX));
            const int top    = static_cast<int>(std::lround(static_cast<double>(box.top()) * inverseScaleY));
            const int right  = static_cast<int>(std::lround(static_cast<double>(box.left() + box.width()) * inverseScaleX));
            const int bottom = static_cast<int>(std::lround(static_cast<double>(box.top() + box.height()) * inverseScaleY));
            return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top)).intersected(imageRect);
        }

        /// 一个输出张量的只读视图：数据在 OrtValue 的生命期内有效
        struct FloatTensorView
        {
            const float               *data         = nullptr;
            std::size_t                elementCount = 0;
            std::vector<std::int64_t>  dimensions;
        };
    } // namespace

    struct PpOcrOnnxEngine::Impl
    {
        /**
         * @brief 检查模型文件并加载运行库
         * @param directory 模型目录，可以尚不存在
         */
        explicit Impl(std::filesystem::path directory);

        /**
         * @brief 释放会话、运行环境与运行库句柄
         * @details 会话先于运行库释放：会话的虚表与线程池都在 DLL 里
         */
        ~Impl();

        Impl(const Impl &)            = delete;
        Impl &operator=(const Impl &) = delete;

        /**
         * @brief 首次调用时创建环境、两个会话与字表，之后原样复用
         * @return 成功返回空载荷；失败返回原因并记住它，不再反复读坏模型
         */
        [[nodiscard]] Base::Expected<void> ensureSessions();

        /**
         * @brief 检测、逐框识别并拼装结果
         * @param image 已过最小尺寸检查的原图
         * @param language 解析后的语言标签
         * @return 成功返回结果；失败返回原因
         */
        [[nodiscard]] Base::Expected<Core::OcrResult> recognizeImage(const QImage &image, const QString &language);

        /**
         * @brief 创建一条会话，取它的输入输出名与输入形状
         * @param modelFileName 模型文件名
         * @param session 成功时接受会话
         * @param inputName 成功时接受输入名
         * @param outputName 成功时接受输出名
         * @param inputShape 成功时接受输入形状，动态维为 -1
         * @return 成功返回空载荷；失败返回带模型文件名的原因
         */
        [[nodiscard]] Base::Expected<void> createSession(const char *modelFileName, OrtHandle<OrtSession> &session, std::string &inputName,
                                                         std::string &outputName, std::vector<std::int64_t> &inputShape);

        /**
         * @brief 把一个输出值按 FLOAT 张量打开
         * @param value 输出值
         * @param modelFileName 出错时写进原因的模型文件名
         * @return 成功返回只读视图；失败返回原因
         */
        [[nodiscard]] Base::Expected<FloatTensorView> floatTensorView(OrtValue *value, const char *modelFileName) const;

        /// 模型目录（构造时给定，不随设置变化）
        std::filesystem::path modelDirectory;

        /// 运行库句柄，析构时最后释放
        HMODULE library = nullptr;

        /// 运行库接口表，句柄为空时也为空
        const OrtApi *api = nullptr;

        /// 构造期查出的结论
        bool    available = false;
        QString unavailableReason;

        /// 会话与张量内存信息，首次识别时创建
        OrtHandle<OrtEnv>       environment;
        OrtHandle<OrtMemoryInfo> memoryInfo;
        OrtHandle<OrtSession>   detSession;
        OrtHandle<OrtSession>   recSession;

        std::string detInputName;
        std::string detOutputName;
        std::string recInputName;
        std::string recOutputName;

        /// 预处理的目标尺寸：模型把某一维固定下来时按模型的值，全动态时用通用上限
        int detMaxSide = Core::kPpOcrDetMaxSide;
        int recHeight  = Core::kPpOcrRecHeight;

        /// 识别字表，解码时按索引取字
        QStringList dictionary;

        /// 第一次建会话失败的原因；有值时后续调用直接回它
        std::optional<Base::Error> sessionFailure;
    };

    PpOcrOnnxEngine::Impl::Impl(std::filesystem::path directory) : modelDirectory(std::move(directory))
    {
        QStringList missingFiles;
        for (const char *fileName: {kPpOcrDetModelFileName, kPpOcrRecModelFileName, kPpOcrDictFileName})
        {
            std::error_code error;
            const auto      size = std::filesystem::file_size(modelDirectory / fileName, error);
            if (error || size == 0)
            {
                missingFiles << QString::fromLatin1(fileName);
            }
        }

        QStringList problems;
        if (!missingFiles.isEmpty())
        {
            problems << QString::fromStdString(Base::uiFormat(
                    Base::uiTextUtf8("PP-OCR 模型文件不在或为空：{}。请重新安装本程序，或在设置的 OCR 页把模型目录指过去"),
                    missingFiles.join(QStringLiteral("、")).toStdString()));
        }

        const QString libraryProblem = openRuntimeLibrary(library, api);
        if (!libraryProblem.isEmpty())
        {
            problems << libraryProblem;
        }

        available         = problems.isEmpty();
        unavailableReason = problems.join(QLatin1Char(' '));
    }

    PpOcrOnnxEngine::Impl::~Impl()
    {
        recSession.reset();
        detSession.reset();
        memoryInfo.reset();
        environment.reset();
        if (library != nullptr)
        {
            FreeLibrary(library);
            library = nullptr;
        }
    }

    Base::Expected<void> PpOcrOnnxEngine::Impl::createSession(const char *modelFileName, OrtHandle<OrtSession> &session, std::string &inputName,
                                                              std::string &outputName, std::vector<std::int64_t> &inputShape)
    {
        OrtSessionOptions *rawOptions = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->CreateSessionOptions(&rawOptions), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        OrtHandle<OrtSessionOptions> options(rawOptions, OrtDeleter<OrtSessionOptions>{api->ReleaseSessionOptions});

        const std::filesystem::path modelPath = modelDirectory / modelFileName;
        OrtSession                 *rawSession = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->CreateSession(environment.get(), modelPath.c_str(), options.get(), &rawSession), api), api);
            !message.isEmpty())
        {
            session = OrtHandle<OrtSession>(rawSession, OrtDeleter<OrtSession>{api->ReleaseSession});
            return std::unexpected(modelFailure(modelFileName, message));
        }
        session = OrtHandle<OrtSession>(rawSession, OrtDeleter<OrtSession>{api->ReleaseSession});

        std::size_t inputCount = 0;
        if (const QString message = consumeStatus(takeStatus(api->SessionGetInputCount(session.get(), &inputCount), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        std::size_t outputCount = 0;
        if (const QString message = consumeStatus(takeStatus(api->SessionGetOutputCount(session.get(), &outputCount), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        if (inputCount == 0 || outputCount == 0)
        {
            return std::unexpected(modelFailure(modelFileName, QString::fromStdString(Base::uiTextUtf8("模型没有输入或没有输出"))));
        }

        OrtAllocator *allocator = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->GetAllocatorWithDefaultOptions(&allocator), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }

        char *rawInputName = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->SessionGetInputName(session.get(), 0, allocator, &rawInputName), api), api); !message.isEmpty())
        {
            freeAllocatorName(api, allocator, rawInputName);
            return std::unexpected(modelFailure(modelFileName, message));
        }
        inputName = rawInputName != nullptr ? rawInputName : "";
        freeAllocatorName(api, allocator, rawInputName);

        char *rawOutputName = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->SessionGetOutputName(session.get(), 0, allocator, &rawOutputName), api), api); !message.isEmpty())
        {
            freeAllocatorName(api, allocator, rawOutputName);
            return std::unexpected(modelFailure(modelFileName, message));
        }
        outputName = rawOutputName != nullptr ? rawOutputName : "";
        freeAllocatorName(api, allocator, rawOutputName);

        OrtTypeInfo *rawTypeInfo = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->SessionGetInputTypeInfo(session.get(), 0, &rawTypeInfo), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        OrtHandle<OrtTypeInfo> typeInfo(rawTypeInfo, OrtDeleter<OrtTypeInfo>{api->ReleaseTypeInfo});

        const OrtTensorTypeAndShapeInfo *tensorInfo = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->CastTypeInfoToTensorInfo(typeInfo.get(), &tensorInfo), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        if (tensorInfo == nullptr)
        {
            return std::unexpected(modelFailure(modelFileName, QString::fromStdString(Base::uiTextUtf8("输入不是张量"))));
        }

        std::size_t dimensionCount = 0;
        if (const QString message = consumeStatus(takeStatus(api->GetDimensionsCount(tensorInfo, &dimensionCount), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        inputShape.assign(dimensionCount, -1);
        if (dimensionCount > 0)
        {
            if (const QString message = consumeStatus(takeStatus(api->GetDimensions(tensorInfo, inputShape.data(), dimensionCount), api), api); !message.isEmpty())
            {
                return std::unexpected(modelFailure(modelFileName, message));
            }
        }
        return {};
    }

    Base::Expected<FloatTensorView> PpOcrOnnxEngine::Impl::floatTensorView(OrtValue *value, const char *modelFileName) const
    {
        OrtTensorTypeAndShapeInfo *rawInfo = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->GetTensorTypeAndShape(value, &rawInfo), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        OrtHandle<OrtTensorTypeAndShapeInfo> info(rawInfo, OrtDeleter<OrtTensorTypeAndShapeInfo>{api->ReleaseTensorTypeAndShapeInfo});

        ONNXTensorElementDataType elementType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
        if (const QString message = consumeStatus(takeStatus(api->GetTensorElementType(info.get(), &elementType), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        if (elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
        {
            return std::unexpected(modelFailure(modelFileName, QString::fromStdString(Base::uiTextUtf8("输出不是 FLOAT 张量"))));
        }

        FloatTensorView view;
        std::size_t     dimensionCount = 0;
        if (const QString message = consumeStatus(takeStatus(api->GetDimensionsCount(info.get(), &dimensionCount), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        view.dimensions.assign(dimensionCount, -1);
        if (dimensionCount > 0)
        {
            if (const QString message = consumeStatus(takeStatus(api->GetDimensions(info.get(), view.dimensions.data(), dimensionCount), api), api);
                !message.isEmpty())
            {
                return std::unexpected(modelFailure(modelFileName, message));
            }
        }

        if (const QString message = consumeStatus(takeStatus(api->GetTensorShapeElementCount(info.get(), &view.elementCount), api), api);
            !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }

        void *rawData = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->GetTensorMutableData(value, &rawData), api), api); !message.isEmpty())
        {
            return std::unexpected(modelFailure(modelFileName, message));
        }
        if (rawData == nullptr)
        {
            return std::unexpected(modelFailure(modelFileName, QString::fromStdString(Base::uiTextUtf8("输出张量为空"))));
        }

        view.data = static_cast<const float *>(rawData);
        return view;
    }

    Base::Expected<void> PpOcrOnnxEngine::Impl::ensureSessions()
    {
        if (sessionFailure.has_value())
        {
            return std::unexpected(*sessionFailure);
        }
        if (detSession != nullptr && recSession != nullptr)
        {
            return {};
        }

        const auto fail = [this](Base::Error error) -> Base::Expected<void>
        {
            sessionFailure = error;
            return std::unexpected(error);
        };

        OrtEnv *rawEnvironment = nullptr;
        if (const QString message = consumeStatus(takeStatus(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "GyanisScribe", &rawEnvironment), api), api);
            !message.isEmpty())
        {
            return fail(internalError(Base::uiFormat(Base::uiTextUtf8("ONNX Runtime 运行环境初始化失败：{}。请重新启动本程序后再试"), message.toStdString())));
        }
        environment = OrtHandle<OrtEnv>(rawEnvironment, OrtDeleter<OrtEnv>{api->ReleaseEnv});

        OrtMemoryInfo *rawMemoryInfo = nullptr;
        if (const QString message =
                    consumeStatus(takeStatus(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &rawMemoryInfo), api), api);
            !message.isEmpty())
        {
            return fail(internalError(Base::uiFormat(Base::uiTextUtf8("ONNX Runtime 张量内存描述创建失败：{}。请重新启动本程序后再试"), message.toStdString())));
        }
        memoryInfo = OrtHandle<OrtMemoryInfo>(rawMemoryInfo, OrtDeleter<OrtMemoryInfo>{api->ReleaseMemoryInfo});

        std::vector<std::int64_t> detShape;
        if (const auto created = createSession(kPpOcrDetModelFileName, detSession, detInputName, detOutputName, detShape); !created)
        {
            return fail(created.error());
        }
        std::vector<std::int64_t> recShape;
        if (const auto created = createSession(kPpOcrRecModelFileName, recSession, recInputName, recOutputName, recShape); !created)
        {
            return fail(created.error());
        }

        // 模型把长边或高度固定下来时，预处理的目标就取模型要的那个值
        if (detShape.size() == 4)
        {
            if (detShape[3] > 0)
            {
                detMaxSide = static_cast<int>(detShape[3]);
            }
            else if (detShape[2] > 0)
            {
                detMaxSide = static_cast<int>(detShape[2]);
            }
        }
        if (recShape.size() == 4 && recShape[2] > 0)
        {
            recHeight = static_cast<int>(recShape[2]);
        }

        QFile dictionaryFile(QString::fromStdWString((modelDirectory / kPpOcrDictFileName).wstring()));
        if (!dictionaryFile.open(QIODevice::ReadOnly))
        {
            return fail(internalError(Base::uiFormat(Base::uiTextUtf8("PP-OCR 字表 {} 读不出来：{}。请重新安装本程序"), kPpOcrDictFileName,
                                                       dictionaryFile.errorString().toStdString())));
        }
        QStringList lines = QString::fromUtf8(dictionaryFile.readAll()).split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        for (auto &line: lines)
        {
            if (line.endsWith(QLatin1Char('\r')))
            {
                line.chop(1);
            }
        }
        // 只去掉文件末尾换行带来的空行；中间的空行占着一个字表索引，不能删
        if (!lines.isEmpty() && lines.constLast().isEmpty())
        {
            lines.removeLast();
        }
        if (lines.isEmpty())
        {
            return fail(internalError(Base::uiFormat(Base::uiTextUtf8("PP-OCR 字表 {} 是空文件。请重新安装本程序"), kPpOcrDictFileName)));
        }
        dictionary = std::move(lines);
        return {};
    }

    Base::Expected<Core::OcrResult> PpOcrOnnxEngine::Impl::recognizeImage(const QImage &image, const QString &language)
    {
        if (const auto ready = ensureSessions(); !ready)
        {
            return std::unexpected(ready.error());
        }

        Core::PpOcrTensor detInput = Core::buildDetInput(image, detMaxSide);
        if (detInput.data.empty())
        {
            return std::unexpected(internalError(Base::uiTextUtf8("PP-OCR 检测输入构造失败：缩放后的图像为空")));
        }

        OrtValue         *rawDetTensor = nullptr;
        const std::int64_t detShape[4] = {1, 3, detInput.height, detInput.width};
        if (const QString message = consumeStatus(takeStatus(api->CreateTensorWithDataAsOrtValue(memoryInfo.get(), detInput.data.data(),
                                                                                                detInput.data.size() * sizeof(float), detShape, 4,
                                                                                                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &rawDetTensor),
                                                                             api),
                                                 api);
            !message.isEmpty())
        {
            return std::unexpected(modelFailure(kPpOcrDetModelFileName, message));
        }
        OrtHandle<OrtValue> detTensor(rawDetTensor, OrtDeleter<OrtValue>{api->ReleaseValue});

        const char     *detInputs[]       = {detInputName.c_str()};
        const OrtValue *detInputValues[]  = {detTensor.get()};
        const char     *detOutputs[]      = {detOutputName.c_str()};
        OrtValue       *detOutputValues[] = {nullptr};
        const QString   detRunError =
                consumeStatus(takeStatus(api->Run(detSession.get(), nullptr, detInputs, detInputValues, 1, detOutputs, 1, detOutputValues), api), api);
        OrtHandle<OrtValue> detOutput(detOutputValues[0], OrtDeleter<OrtValue>{api->ReleaseValue});
        if (!detRunError.isEmpty())
        {
            return std::unexpected(modelFailure(kPpOcrDetModelFileName, detRunError));
        }

        const auto detView = floatTensorView(detOutput.get(), kPpOcrDetModelFileName);
        if (!detView)
        {
            return std::unexpected(detView.error());
        }
        if (detView->dimensions.size() < 2)
        {
            return std::unexpected(modelFailure(kPpOcrDetModelFileName, QString::fromStdString(Base::uiTextUtf8("输出维数不足，取不到概率图尺寸"))));
        }
        const std::int64_t mapHeight = detView->dimensions[detView->dimensions.size() - 2];
        const std::int64_t mapWidth  = detView->dimensions.back();
        if (mapHeight <= 0 || mapWidth <= 0)
        {
            return std::unexpected(modelFailure(kPpOcrDetModelFileName, QString::fromStdString(Base::uiTextUtf8("输出尺寸不是正数"))));
        }
        if (detView->elementCount != static_cast<std::size_t>(mapWidth) * static_cast<std::size_t>(mapHeight))
        {
            return std::unexpected(modelFailure(kPpOcrDetModelFileName, QString::fromStdString(Base::uiTextUtf8("输出元素数与概率图尺寸不符"))));
        }

        const QVector<Core::PpOcrTextBox> textBoxes =
                Core::extractTextBoxes(detView->data, static_cast<int>(mapWidth), static_cast<int>(mapHeight));

        Core::OcrResult result;
        result.language  = language;
        double weightedConfidence = 0.0;
        double weightSum          = 0.0;

        for (const auto &box: textBoxes)
        {
            const QRect cropRect = mapBackToImage(box.rect, detInput, image.rect());
            if (cropRect.isEmpty())
            {
                continue;
            }

            Core::PpOcrTensor recInput = Core::buildRecInput(image.copy(cropRect), recHeight, Core::kPpOcrRecMaxWidth);
            if (recInput.data.empty())
            {
                continue;
            }

            OrtValue          *rawRecTensor = nullptr;
            const std::int64_t recShape[4]  = {1, 3, recInput.height, recInput.width};
            if (const QString message = consumeStatus(takeStatus(api->CreateTensorWithDataAsOrtValue(memoryInfo.get(), recInput.data.data(),
                                                                                                    recInput.data.size() * sizeof(float), recShape, 4,
                                                                                                    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &rawRecTensor),
                                                                                 api),
                                                     api);
                !message.isEmpty())
            {
                return std::unexpected(modelFailure(kPpOcrRecModelFileName, message));
            }
            OrtHandle<OrtValue> recTensor(rawRecTensor, OrtDeleter<OrtValue>{api->ReleaseValue});

            const char     *recInputs[]       = {recInputName.c_str()};
            const OrtValue *recInputValues[]  = {recTensor.get()};
            const char     *recOutputs[]      = {recOutputName.c_str()};
            OrtValue       *recOutputValues[] = {nullptr};
            const QString   recRunError       = consumeStatus(
                    takeStatus(api->Run(recSession.get(), nullptr, recInputs, recInputValues, 1, recOutputs, 1, recOutputValues), api), api);
            OrtHandle<OrtValue> recOutput(recOutputValues[0], OrtDeleter<OrtValue>{api->ReleaseValue});
            if (!recRunError.isEmpty())
            {
                return std::unexpected(modelFailure(kPpOcrRecModelFileName, recRunError));
            }

            const auto recView = floatTensorView(recOutput.get(), kPpOcrRecModelFileName);
            if (!recView)
            {
                return std::unexpected(recView.error());
            }
            if (recView->dimensions.size() < 2)
            {
                return std::unexpected(modelFailure(kPpOcrRecModelFileName, QString::fromStdString(Base::uiTextUtf8("输出维数不足，取不到时间步与类别数"))));
            }
            const std::int64_t timeSteps    = recView->dimensions[recView->dimensions.size() - 2];
            const std::int64_t classCount   = recView->dimensions.back();
            if (timeSteps <= 0 || classCount <= 0)
            {
                return std::unexpected(modelFailure(kPpOcrRecModelFileName, QString::fromStdString(Base::uiTextUtf8("输出尺寸不是正数"))));
            }
            if (recView->elementCount != static_cast<std::size_t>(timeSteps) * static_cast<std::size_t>(classCount))
            {
                return std::unexpected(modelFailure(kPpOcrRecModelFileName, QString::fromStdString(Base::uiTextUtf8("输出元素数与时间步乘类别数不符"))));
            }

            const Core::PpOcrDecodedText decoded =
                    Core::decodeCtc(recView->data, static_cast<int>(timeSteps), static_cast<int>(classCount), dictionary);
            if (decoded.text.isEmpty())
            {
                continue;
            }

            Core::OcrLine line;
            line.words       = {Core::OcrWord{decoded.text, cropRect}};
            line.boundingBox = cropRect;
            result.lines.push_back(line);

            const double weight = static_cast<double>(decoded.text.size());
            weightedConfidence += decoded.confidence * weight;
            weightSum += weight;
        }

        if (result.lines.isEmpty() || weightSum <= 0.0)
        {
            // 一行都没认出来就是空结果，置信度留空而不是打一个 0
            return result;
        }
        result.averageConfidence = weightedConfidence / weightSum;
        return result;
    }

    PpOcrOnnxEngine::PpOcrOnnxEngine(std::filesystem::path modelDirectory) : m_impl(std::make_unique<Impl>(std::move(modelDirectory)))
    {
    }

    PpOcrOnnxEngine::~PpOcrOnnxEngine() = default;

    QString PpOcrOnnxEngine::name() const
    {
        return QStringLiteral("PaddleOCR");
    }

    bool PpOcrOnnxEngine::isAvailable() const
    {
        return m_impl->available;
    }

    QString PpOcrOnnxEngine::unavailableReason() const
    {
        return m_impl->unavailableReason;
    }

    QStringList PpOcrOnnxEngine::supportedLanguages() const
    {
        if (!m_impl->available)
        {
            return {};
        }
        return {QStringLiteral("zh-Hans"), QStringLiteral("en")};
    }

    Base::Expected<Core::OcrResult> PpOcrOnnxEngine::recognize(const QImage &image, const QString &language)
    {
        if (image.isNull() || image.width() < kMinimumImageEdge || image.height() < kMinimumImageEdge)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument,
                                               Base::uiFormat(Base::uiTextUtf8("图片太小或为空（{}×{}），认不出字。请至少框住 {}×{} 的区域"),
                                                              image.width(), image.height(), kMinimumImageEdge, kMinimumImageEdge)});
        }

        if (!m_impl->available)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported,
                                               Base::uiFormat(Base::uiTextUtf8("PaddleOCR 后端不可用：{}。请重新安装本程序，或在设置的 OCR 页检查模型目录"),
                                                              m_impl->unavailableReason.toStdString())});
        }

        const QString requestedLanguage = language.isEmpty() ? QString::fromUtf8(Core::kDefaultOcrLanguage) : language;
        if (!supportedLanguages().contains(requestedLanguage, Qt::CaseInsensitive))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported,
                                               Base::uiFormat(Base::uiTextUtf8("PaddleOCR 后端不支持语言「{}」，可用的有：{}。请在设置的 OCR 页换一种语言"),
                                                              requestedLanguage.toStdString(), supportedLanguages().join(QStringLiteral("、")).toStdString())});
        }
        return m_impl->recognizeImage(image, requestedLanguage);
    }

} // namespace GyanisScribe::Plugins
