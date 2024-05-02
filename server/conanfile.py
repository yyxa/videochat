from conan import ConanFile

class Audio2WaveformConan(ConanFile):
  settings = "os", "compiler", "build_type", "arch"
  generators = "CMakeDeps", "CMakeToolchain"

  def requirements(self):
    self.requires("boost/1.81.0")
    self.requires("libpqxx/7.9.0")
    self.requires("nlohmann_json/3.11.3")
    self.requires("redis-plus-plus/1.3.12")
    self.requires("cassandra-cpp-driver/2.17.1")