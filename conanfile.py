from conan import ConanFile
from conan.tools.cmake import cmake_layout, CMakeToolchain


class ConanApplication(ConanFile):
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    # 依赖清单的唯一出处是 conandata.yml；本文件与它都刻意不带 IDE 插件那两行「内容会被覆盖」的托管标记，
    # 否则插件下一次写入会把手工加的 requires 整片抹掉。版本号也不在这里重复：工程侧 project(... VERSION)
    # 才是唯一真源，包配方再抄一份会在某一天对不上。

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.generate()

    def requirements(self):
        for requirement in self.conan_data.get("requirements", []):
            self.requires(requirement)

        # Debug 档不领 Tesseract：上游 leptonica 的 Debug 包与其 cpp_info 的库名不一致，
        # tesseract 在 Debug 下 configure 一定失败（缺的库是 leptonica-1.87.0）。Release 档照领，
        # 于是同一份源码在两档里各编一种形状：Release 带真后端，Debug 带降级桩。
        # 撤掉这条判断的判据：conan install -s build_type=Debug --requires=tesseract/5.5.2 能 configure 过。
        if str(self.settings.build_type) != "Debug":
            for requirement in self.conan_data.get("releaseOnlyRequirements", []):
                self.requires(requirement)
