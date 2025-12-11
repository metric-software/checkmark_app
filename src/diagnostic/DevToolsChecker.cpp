#include "DevToolsChecker.h"

DevToolsChecker::DevToolsChecker(QObject* parent) : QObject(parent) {}

void DevToolsChecker::checkAllTools() {
  emit logMessage("\n===============================================");
  emit logMessage("Starting Developer Tools Check");
  emit logMessage("===============================================\n");

  devToolsResults.clear();  // Clear previous results

  checkPythonInstalls();
  checkNodeInstall();
  checkGitInstall();
  checkJavaInstalls();
  checkCudaInstall();
  checkCuDNNInstall();
  checkFFmpegInstall();
  checkVSInstall();

  emit logMessage("\n===============================================");
  emit logMessage("Developer Tools Check Completed");
  emit logMessage("===============================================\n");

  emit toolCheckCompleted(devToolsResults);
}

void DevToolsChecker::addResult(const QString& tool, bool found,
                                const QString& version) {
  emit logMessage(
    QString("[%1] Status: %2").arg(tool).arg(found ? "Found" : "Not Found"));
  if (!version.isEmpty()) {
    emit logMessage(QString("[%1] Version: %2").arg(tool, version));
  }
  emit logMessage("-----------------------------------------------");

  devToolsResults += QString("%1:\t<span style='color: %2;'>%3</span><br>")
                       .arg(tool)
                       .arg(found ? "#0078d4" : "#ff4444")
                       .arg(found ? version : QString("Not Found"));

  emit toolCheckResult(tool, found, version);
}

void DevToolsChecker::checkPythonInstalls() {
  QProcess process;

  // Check Python versions
  QStringList pythonCommands = {"python --version", "python3 --version",
                                "py --version"};

  for (const QString& cmd : pythonCommands) {
    process.start(cmd);
    process.waitForFinished();
    QString output = process.readAllStandardOutput();
    QString error = process.readAllStandardError();

    if (process.exitCode() == 0) {
      emit logMessage(
        QString("Found %1: %2").arg(cmd.split(' ')[0], output.trimmed()));
    }
  }

  // Check common install paths
  QStringList commonPaths = {"C:/Python27",
                             "C:/Python37",
                             "C:/Python38",
                             "C:/Python39",
                             "C:/Python310",
                             "C:/Python311",
                             "C:/Users/" + qEnvironmentVariable("USERNAME") +
                               "/AppData/Local/Programs/Python"};

  for (const QString& path : commonPaths) {
    if (QDir(path).exists()) {
      emit logMessage(QString("Found Python installation at: %1").arg(path));
    }
  }

  // Check PATH environment variable
  QString pathEnv = qEnvironmentVariable("PATH");
  QStringList paths = pathEnv.split(';');
  for (const QString& path : paths) {
    if (path.contains("Python", Qt::CaseInsensitive)) {
      emit logMessage(QString("Python in PATH: %1").arg(path));
    }
  }

  // Check for Python
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start("python", QStringList() << "--version");
  process.waitForFinished(3000);
  QString pythonVersion = (process.exitCode() == 0)
                            ? QString(process.readAllStandardOutput()).trimmed()
                            : QString();
  addResult("Python", process.exitCode() == 0, pythonVersion);
}

void DevToolsChecker::checkNodeInstall() {
  QProcess process;

  // Check Node.js version
  process.start("node --version");
  process.waitForFinished();
  if (process.exitCode() == 0) {
    emit logMessage(QString("Node.js version: %1")
                      .arg(QString(process.readAllStandardOutput()).trimmed()));
  }

  // Check npm version
  process.start("npm --version");
  process.waitForFinished();
  if (process.exitCode() == 0) {
    emit logMessage(QString("npm version: %1")
                      .arg(QString(process.readAllStandardOutput()).trimmed()));
  }

  // Check common install paths
  QStringList nodePaths = {"C:/Program Files/nodejs",
                           "C:/Program Files (x86)/nodejs"};

  for (const QString& path : nodePaths) {
    if (QDir(path).exists()) {
      emit logMessage(QString("Found Node.js installation at: %1").arg(path));
    }
  }

  // Add to results
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start("node", QStringList() << "--version");
  process.waitForFinished(3000);
  QString nodeVersion = (process.exitCode() == 0)
                          ? QString(process.readAllStandardOutput()).trimmed()
                          : QString();
  addResult("Node.js", process.exitCode() == 0, nodeVersion);
}

void DevToolsChecker::checkGitInstall() {
  QProcess process;

  // Check Git version
  process.start("git --version");
  process.waitForFinished();
  if (process.exitCode() == 0) {
    emit logMessage(QString("Git version: %1")
                      .arg(QString(process.readAllStandardOutput()).trimmed()));
  }

  // Check for global .gitconfig
  QString homePath = QDir::homePath();
  QString gitConfig = homePath + "/.gitconfig";
  if (QFile::exists(gitConfig)) {
    emit logMessage("Found git config file");

    // Optionally read user.name and user.email
    QFile file(gitConfig);
    if (file.open(QIODevice::ReadOnly)) {
      QString config = file.readAll();
      emit logMessage("Git config contents:\n" + config);
    }
  }

  // Check common install paths
  QStringList gitPaths = {"C:/Program Files/Git", "C:/Program Files (x86)/Git"};

  for (const QString& path : gitPaths) {
    if (QDir(path).exists()) {
      emit logMessage(QString("Found Git installation at: %1").arg(path));
    }
  }

  // Add to results
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start("git", QStringList() << "--version");
  process.waitForFinished(3000);
  QString gitVersion = (process.exitCode() == 0)
                         ? QString(process.readAllStandardOutput()).trimmed()
                         : QString();
  addResult("Git", process.exitCode() == 0, gitVersion);
}

void DevToolsChecker::checkJavaInstalls() {
  QProcess process;

  // Check Java version
  process.start("java -version");
  process.waitForFinished();
  QString output =
    process.readAllStandardError();  // Java outputs version to stderr
  if (process.exitCode() == 0) {
    emit logMessage("Java version info:\n" + output.trimmed());
  }

  // Check JAVA_HOME
  QString javaHome = qEnvironmentVariable("JAVA_HOME");
  if (!javaHome.isEmpty()) {
    emit logMessage(QString("JAVA_HOME: %1").arg(javaHome));

    // Check if directory exists
    QDir javaDir(javaHome);
    if (javaDir.exists()) {
      // Check for key Java files/folders
      if (QFile::exists(javaHome + "/bin/java.exe")) {
        emit logMessage("Found Java executable in JAVA_HOME");
      }
    }
  }

  // Check common JDK install locations
  QStringList jdkPaths = {"C:/Program Files/Java",
                          "C:/Program Files (x86)/Java",
                          QDir::homePath() + "/.jdks"};

  for (const QString& basePath : jdkPaths) {
    QDir baseDir(basePath);
    if (baseDir.exists()) {
      // List all JDK directories
      QStringList jdkDirs =
        baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
      for (const QString& jdk : jdkDirs) {
        if (jdk.contains("jdk", Qt::CaseInsensitive)) {
          emit logMessage(
            QString("Found JDK installation: %1/%2").arg(basePath, jdk));
        }
      }
    }
  }

  // Check PATH for Java entries
  QString pathEnv = qEnvironmentVariable("PATH");
  QStringList paths = pathEnv.split(';');
  for (const QString& path : paths) {
    if (path.contains("Java", Qt::CaseInsensitive) ||
        path.contains("jdk", Qt::CaseInsensitive)) {
      emit logMessage(QString("Java in PATH: %1").arg(path));
    }
  }

  // Add to results
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start("java", QStringList() << "-version");
  process.waitForFinished(3000);
  QString javaVersion =
    (process.exitCode() == 0)
      ? QString(process.readAllStandardError()).split('\n')[0].trimmed()
      : QString();
  addResult("Java", process.exitCode() == 0, javaVersion);
}

void DevToolsChecker::checkCudaInstall() {
  emit logMessage("\n===============================================");
  emit logMessage("Checking CUDA Installation");
  emit logMessage("===============================================\n");

  // Check CUDA_PATH
  QString cudaPath = qEnvironmentVariable("CUDA_PATH");
  if (!cudaPath.isEmpty()) {
    emit logMessage("CUDA_PATH environment variable found:");
    emit logMessage(QString("  → %1").arg(cudaPath));
  } else {
    emit logMessage("CUDA_PATH environment variable not found");
  }
  emit logMessage("-----------------------------------------------");

  // Check installations
  QStringList cudaPaths = {
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8",
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.0",
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.1",
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.2"};

  bool foundAny = false;
  QString cudaVersion;

  for (const QString& path : cudaPaths) {
    if (QDir(path).exists()) {
      emit logMessage(QString("Found CUDA installation: %1").arg(path));

      QString nvccPath = path + "/bin/nvcc.exe";
      if (QFile::exists(nvccPath)) {
        emit logMessage("  → NVCC compiler found");
        QProcess process;
        process.start(nvccPath + " --version");
        process.waitForFinished();
        if (process.exitCode() == 0) {
          QString output = QString(process.readAllStandardOutput()).trimmed();
          emit logMessage("  → NVCC version info:");
          emit logMessage(QString("    %1").arg(output));
          cudaVersion = output;
        }
      } else {
        emit logMessage("  → NVCC compiler not found");
      }
      foundAny = true;
    }
  }

  if (!foundAny) {
    emit logMessage("No CUDA installations found in standard locations");
  }

  emit logMessage("\n===============================================");

  // Add to results
  addResult("CUDA", foundAny, cudaVersion);
}

void DevToolsChecker::checkCuDNNInstall() {
  emit logMessage("\n===============================================");
  emit logMessage("Checking cuDNN Installation");
  emit logMessage("===============================================\n");

  bool cudnnFound = false;
  QString cudnnVersion = "";

  // Check PATH first
  emit logMessage("Checking PATH for cuDNN...");
  QString pathEnv = qEnvironmentVariable("PATH");
  QStringList paths = pathEnv.split(';');

  for (const QString& path : paths) {
    QDir dir(path);
    QStringList files =
      dir.entryList(QStringList() << "cudnn*.dll", QDir::Files);
    if (!files.isEmpty()) {
      emit logMessage(QString("Found cuDNN in PATH: %1").arg(path));
      emit logMessage("DLL files found:");
      for (const QString& file : files) {
        emit logMessage(QString("  → %1").arg(file));
        if (file.contains("64_8")) {
          cudnnVersion = "v8.x";
        } else if (file.contains("64_7")) {
          cudnnVersion = "v7.x";
        }
      }
      cudnnFound = true;
    }
  }
  emit logMessage("-----------------------------------------------");

  // Check CUDA directories
  QString cudaPath = qEnvironmentVariable("CUDA_PATH");
  if (!cudaPath.isEmpty()) {
    emit logMessage("Checking CUDA directories...");
    QStringList cudaDirs = {cudaPath + "/include", cudaPath + "/lib/x64",
                            cudaPath + "/bin"};

    for (const QString& dir : cudaDirs) {
      QDir checkDir(dir);
      if (checkDir.exists()) {
        // Check for header and try to parse version
        if (dir.contains("include") && QFile::exists(dir + "/cudnn.h")) {
          emit logMessage(QString("Found cuDNN header in: %1").arg(dir));
          QFile headerFile(dir + "/cudnn.h");
          if (headerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&headerFile);
            QString line;
            while (!(line = in.readLine()).isNull()) {
              if (line.contains("CUDNN_MAJOR")) {
                QRegularExpression re("\\d+");
                auto match = re.match(line);
                if (match.hasMatch()) {
                  cudnnVersion = "v" + match.captured() + ".x";
                  break;
                }
              }
            }
            headerFile.close();
          }
          cudnnFound = true;
        }

        // Check for DLLs and libs
        if (dir.contains("bin") || dir.contains("lib")) {
          QStringList filters = {"cudnn*.dll", "cudnn*.lib"};
          QStringList files = checkDir.entryList(filters, QDir::Files);
          if (!files.isEmpty()) {
            emit logMessage(QString("Found cuDNN files in: %1").arg(dir));
            for (const QString& file : files) {
              emit logMessage(QString("  → %1").arg(file));
            }
            cudnnFound = true;
          }
        }
      }
    }
  }
  emit logMessage("-----------------------------------------------");

  // Additional common cuDNN locations
  QStringList additionalPaths = {
    "C:/Program Files/NVIDIA/CUDNN/v8.x/bin",
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDNN/v8.x/bin"};

  for (const QString& path : additionalPaths) {
    QDir dir(path);
    if (dir.exists()) {
      QStringList files =
        dir.entryList(QStringList() << "cudnn*.dll", QDir::Files);
      if (!files.isEmpty()) {
        emit logMessage(
          QString("Found cuDNN in additional path: %1").arg(path));
        cudnnFound = true;
        if (cudnnVersion.isEmpty()) {
          cudnnVersion = path.mid(path.indexOf("v"));
        }
      }
    }
  }

  // Summary
  if (cudnnFound) {
    QString version = cudnnVersion.isEmpty() ? "version unknown" : cudnnVersion;
    emit logMessage(QString("cuDNN installation detected (%1)").arg(version));
  } else {
    emit logMessage("No cuDNN installation found");
  }

  emit logMessage("\n===============================================");

  // Add to results
  addResult("cuDNN", cudnnFound, cudnnVersion);
}

void DevToolsChecker::checkFFmpegInstall() {
  QProcess process;

  // Check FFmpeg version
  process.start("ffmpeg -version");
  process.waitForFinished();
  QString ffmpegVersion;

  if (process.exitCode() == 0) {
    QString output = process.readAllStandardOutput();
    QStringList lines = output.split('\n');
    if (!lines.isEmpty()) {
      ffmpegVersion = lines[0].trimmed();
      emit logMessage("FFmpeg: " +
                      ffmpegVersion);  // Just log first line with version
    }
  }

  // Check common install paths
  QStringList ffmpegPaths = {"C:/ffmpeg/bin", "C:/Program Files/ffmpeg/bin",
                             QDir::homePath() + "/ffmpeg/bin"};

  for (const QString& path : ffmpegPaths) {
    if (QDir(path).exists()) {
      emit logMessage(QString("Found FFmpeg installation: %1").arg(path));
    }
  }

  // Check PATH
  QString pathEnv = qEnvironmentVariable("PATH");
  QStringList paths = pathEnv.split(';');
  for (const QString& path : paths) {
    if (path.contains("ffmpeg", Qt::CaseInsensitive)) {
      emit logMessage(QString("FFmpeg in PATH: %1").arg(path));
    }
  }

  // Add to results
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start("ffmpeg", QStringList() << "-version");
  process.waitForFinished(3000);
  addResult("FFmpeg", process.exitCode() == 0, ffmpegVersion);
}

void DevToolsChecker::checkVSInstall() {
  // Check for VS installation using vswhere
  QProcess process;
  QString vsWherePath =
    "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe";
  QString vsVersion;

  if (QFile::exists(vsWherePath)) {
    process.start(vsWherePath, {"-latest", "-property", "displayName"});
    process.waitForFinished();
    if (process.exitCode() == 0) {
      vsVersion = QString(process.readAllStandardOutput()).trimmed();
      emit logMessage("Visual Studio: " + vsVersion);
    }
  }

  // Check for Build Tools
  QStringList buildToolPaths = {
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools",
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools"};

  for (const QString& path : buildToolPaths) {
    if (QDir(path).exists()) {
      emit logMessage(QString("Found VS Build Tools: %1").arg(path));
    }
  }

  // Check for Windows SDK
  QStringList sdkPaths = {"C:/Program Files (x86)/Windows Kits/10",
                          "C:/Program Files (x86)/Windows Kits/8.1"};

  for (const QString& path : sdkPaths) {
    if (QDir(path).exists()) {
      emit logMessage(QString("Found Windows SDK: %1").arg(path));
    }
  }

  // Check if VS is installed
  bool vsInstalled = QFile::exists(vsWherePath) && process.exitCode() == 0;
  addResult("Visual Studio", vsInstalled, vsVersion);
}

void DevToolsChecker::checkAdditionalTools() {
  emit logMessage("\n=== Additional Tools Check ===\n");

  // Check WSL
  {
    emit logMessage("Checking WSL...");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("wsl", QStringList() << "--status");
    process.waitForFinished(3000);
    QString output = process.readAllStandardOutput().trimmed();
    if (process.exitCode() == 0) {
      emit logMessage("Found WSL:\n" + output);
      addResult("WSL", true, output.split('\n')[0]);
    } else {
      emit logMessage("WSL not found");
      addResult("WSL", false);
    }
  }

  // Check Docker
  {
    emit logMessage("Checking Docker...");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("docker", QStringList() << "--version");
    process.waitForFinished(3000);
    QString output = process.readAllStandardOutput().trimmed();
    if (process.exitCode() == 0) {
      emit logMessage("Found Docker:\n" + output);
      addResult("Docker", true, output);
    } else {
      emit logMessage("Docker not found");
      addResult("Docker", false);
    }
  }

  // Check Gradle
  {
    emit logMessage("Checking Gradle...");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("gradle", QStringList() << "--version");
    process.waitForFinished(3000);
    QString output = process.readAllStandardOutput().trimmed();
    if (process.exitCode() == 0) {
      emit logMessage("Found Gradle:\n" + output);
      QStringList lines = output.split('\n');
      addResult("Gradle", true, lines.isEmpty() ? "Unknown" : lines[0]);
    } else {
      emit logMessage("Gradle not found");
      addResult("Gradle", false);
    }
  }

  // Check Maven
  {
    emit logMessage("Checking Maven...");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("mvn", QStringList() << "--version");
    process.waitForFinished(3000);
    QString output = process.readAllStandardOutput().trimmed();
    if (process.exitCode() == 0) {
      emit logMessage("Found Maven:\n" + output);
      QStringList lines = output.split('\n');
      addResult("Maven", true, lines.isEmpty() ? "Unknown" : lines[0]);
    } else {
      emit logMessage("Maven not found");
      addResult("Maven", false);
    }
  }

  // Check CMake
  {
    emit logMessage("Checking CMake...");
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("cmake", QStringList() << "--version");
    process.waitForFinished(3000);
    QString output = process.readAllStandardOutput().trimmed();
    if (process.exitCode() == 0) {
      emit logMessage("Found CMake:\n" + output);
      QStringList lines = output.split('\n');
      addResult("CMake", true, lines.isEmpty() ? "Unknown" : lines[0]);
    } else {
      emit logMessage("CMake not found");
      addResult("CMake", false);
    }
  }

  // Check MinGW
  {
    emit logMessage("Checking MinGW...");
    QStringList mingwPaths = {"C:/MinGW", "C:/Program Files/mingw-w64",
                              "C:/msys64/mingw64"};
    bool foundMinGW = false;
    QString mingwVersion;

    for (const QString& path : mingwPaths) {
      if (QDir(path).exists()) {
        emit logMessage(QString("Found MinGW installation at: %1").arg(path));
        QString gccPath = path + "/bin/gcc.exe";
        if (QFile::exists(gccPath)) {
          QProcess process;
          process.setProcessChannelMode(QProcess::MergedChannels);
          process.start(gccPath, QStringList() << "--version");
          process.waitForFinished(3000);
          if (process.exitCode() == 0) {
            QString output = process.readAllStandardOutput().trimmed();
            emit logMessage("GCC version info:\n" + output);
            QStringList lines = output.split('\n');
            if (!lines.isEmpty()) {
              mingwVersion = lines[0];
            }
          }
        }
        foundMinGW = true;
        break;
      }
    }

    addResult("MinGW (GCC)", foundMinGW,
              mingwVersion.isEmpty()
                ? (foundMinGW ? "Found but version unknown" : "")
                : mingwVersion);
  }

  emit logMessage("Additional tools check completed");
}
