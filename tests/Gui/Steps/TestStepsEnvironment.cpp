#include "TestStepsEnvironment.h"

#include <QApplication>

#include <gtest/gtest.h>

#include <memory>

namespace
{
    /**
     * @brief TestGuiSteps 进程内唯一的 QApplication 所属环境
     * @details 控件、字体量度与 QSignalSpy 都以应用对象为前提；每个 fixture 各建一份会在
     *          同一进程里重复创建，而按套件过滤时又可能一份都没有。
     */
    class StepsApplicationEnvironment : public ::testing::Environment
    {
    public:
        /**
         * @brief 创建应用对象
         */
        void SetUp() override
        {
            // argv 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestGuiSteps";
            static char *arguments[]    = {argumentZero, nullptr};
            m_application               = std::make_unique<QApplication>(argumentCount, arguments);
        }

        /**
         * @brief 销毁应用对象
         */
        void TearDown() override
        {
            m_application.reset();
        }

    private:
        std::unique_ptr<QApplication> m_application; ///< 进程内唯一的应用对象
    };
} // namespace

namespace GyanisScribe::TestSupport
{
    void registerStepsTestEnvironment()
    {
        static const bool registered = []
        {
            ::testing::AddGlobalTestEnvironment(new StepsApplicationEnvironment);
            return true;
        }();
        (void) registered;
    }
} // namespace GyanisScribe::TestSupport

namespace
{
    [[maybe_unused]] const bool kStepsTestEnvironmentRegistered = (GyanisScribe::TestSupport::registerStepsTestEnvironment(), true);
} // namespace
