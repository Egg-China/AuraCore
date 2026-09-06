set(AURACORE_CORE_SOURCES
    ApplicationFwd.h
    CoreApplication.h
    CoreApplication.cpp

    ApplicationMessage.h
    ApplicationMessage.cpp
    AssertHelpers.h
    BaseInstaller.h
    BaseInstaller.cpp
    BaseInstance.h
    BaseInstance.cpp
    BaseVersion.h
    BaseVersionList.h
    BaseVersionList.cpp
    Commandline.h
    Commandline.cpp
    DesktopServices.h
    DesktopServices.cpp
    EnumWrapper.h
    Exception.h
    ExponentialSeries.h
    FileSystem.h
    FileSystem.cpp
    Filter.h
    GZip.h
    GZip.cpp
    HardwareInfo.h
    HardwareInfo.cpp
    InstanceCopyPrefs.h
    InstanceCopyPrefs.cpp
    InstanceCopyTask.h
    InstanceCopyTask.cpp
    InstanceDirUpdate.h
    InstanceDirUpdate.cpp
    InstanceImportTask.h
    InstanceImportTask.cpp
    InstanceList.h
    InstanceList.cpp
    InstanceTask.h
    InstanceTask.cpp
    JavaCommon.h
    JavaCommon.cpp
    Json.h
    Json.cpp
    LaunchController.h
    LaunchController.cpp
    LaunchMode.h
    LibraryUtils.h
    LibraryUtils.cpp
    LoggedProcess.h
    LoggedProcess.cpp
    MessageLevel.h
    MessageLevel.cpp
    MMCTime.h
    MMCTime.cpp
    MMCZip.h
    MMCZip.cpp
    MTPixmapCache.h
    NullInstance.h
    QObjectPtr.h
    ProblemProvider.h
    PSaveFile.h
    qtlogging.ini
    QVariantUtils.h
    RecursiveFileSystemWatcher.h
    RecursiveFileSystemWatcher.cpp
    ResourceDownloadTask.h
    ResourceDownloadTask.cpp
    RuntimeContext.h
    RWStorage.h
    SeparatorPrefixTree.h
    StringUtils.h
    StringUtils.cpp
    SysInfo.h
    SysInfo.cpp
    Usable.h
    Version.h
    Version.cpp
    VersionProxyModel.h
    VersionProxyModel.cpp
    WatchLock.h

    console/Console.h
    include/base.pch.hpp
    include/qtcore.pch.hpp
    include/qtgui.pch.hpp

    logs/AnonymizeLog.h
    logs/AnonymizeLog.cpp
    logs/LogParser.h
    logs/LogParser.cpp
    java/JavaChecker.h
    java/JavaChecker.cpp
    java/JavaInstall.h
    java/JavaInstall.cpp
    java/JavaInstallList.h
    java/JavaInstallList.cpp
    java/JavaUtils.h
    java/JavaUtils.cpp
    java/JavaVersion.h
    java/JavaVersion.cpp
    java/download/ArchiveDownloadTask.h
    java/download/ArchiveDownloadTask.cpp
    java/download/ManifestDownloadTask.h
    java/download/ManifestDownloadTask.cpp

    launch/LaunchStep.h
    launch/LaunchStep.cpp
    launch/LaunchTask.h
    launch/LaunchTask.cpp
    launch/LogModel.h
    launch/LogModel.cpp
    launch/steps/CheckJava.cpp
    launch/steps/CheckJava.h
    launch/steps/LookupServerAddress.cpp
    launch/steps/LookupServerAddress.h
    launch/steps/PostLaunchCommand.cpp
    launch/steps/PostLaunchCommand.h
    launch/steps/PreLaunchCommand.cpp
    launch/steps/PreLaunchCommand.h
    launch/steps/PrintServers.cpp
    launch/steps/PrintServers.h
    launch/steps/TextPrint.cpp
    launch/steps/TextPrint.h


    net/ApiHeaderProxy.h
    net/ApiRequest.h
    net/ByteArraySink.h
    net/ChecksumValidator.h
    net/DummySink.h
    net/FileSink.h
    net/FileSink.cpp
    net/HeaderProxy.h
    net/HttpMetaCache.h
    net/HttpMetaCache.cpp
    net/Logging.h
    net/Logging.cpp
    net/MetaCacheSink.h
    net/MetaCacheSink.cpp
    net/Mode.h
    net/NetJob.h
    net/NetJob.cpp
    net/NetUtils.h
    net/PasteUpload.h
    net/PasteUpload.cpp
    net/RawHeaderProxy.h
    net/Request.h
    net/Request.cpp
    net/Sink.h
    net/Validator.h

    meta/BaseEntity.h
    meta/BaseEntity.cpp
    meta/Index.h
    meta/Index.cpp
    meta/Version.h
    meta/Version.cpp
    meta/VersionList.h
    meta/VersionList.cpp


    settings/INIFile.cpp
    settings/INIFile.h
    settings/INISettingsObject.cpp
    settings/INISettingsObject.h
    settings/OverrideSetting.cpp
    settings/OverrideSetting.h
    settings/PassthroughSetting.cpp
    settings/PassthroughSetting.h
    settings/Setting.cpp
    settings/Setting.h
    settings/SettingsObject.cpp
    settings/SettingsObject.h

    tasks/ConcurrentTask.h
    tasks/ConcurrentTask.cpp
    tasks/MultipleOptionsTask.h
    tasks/MultipleOptionsTask.cpp
    tasks/SequentialTask.h
    tasks/SequentialTask.cpp
    tasks/Task.h
    tasks/Task.cpp

    tools/BaseExternalTool.h
    tools/BaseExternalTool.cpp
    tools/BaseProfiler.h
    tools/BaseProfiler.cpp
    tools/GenericProfiler.h
    tools/GenericProfiler.cpp
    tools/JProfiler.h
    tools/JProfiler.cpp
    tools/JVisualVM.h
    tools/JVisualVM.cpp
)

file(GLOB AURACORE_MINECRAFT_SOURCES CONFIGURE_DEPENDS
    minecraft/*.cpp
    minecraft/*.h
    minecraft/auth/*.cpp
    minecraft/auth/*.h
    minecraft/auth/steps/*.cpp
    minecraft/auth/steps/*.h
    minecraft/launch/*.cpp
    minecraft/launch/*.h
    minecraft/mod/*.cpp
    minecraft/mod/*.h
    minecraft/mod/tasks/*.cpp
    minecraft/mod/tasks/*.h
    minecraft/update/*.cpp
    minecraft/update/*.h
)

file(GLOB AURACORE_MODPLATFORM_SOURCES CONFIGURE_DEPENDS
    modplatform/*.cpp
    modplatform/*.h
    modplatform/atlauncher/*.cpp
    modplatform/atlauncher/*.h
    modplatform/flame/*.cpp
    modplatform/flame/*.h
    modplatform/ftb/*.cpp
    modplatform/ftb/*.h
    modplatform/legacy_ftb/*.cpp
    modplatform/legacy_ftb/*.h
    modplatform/modrinth/*.cpp
    modplatform/modrinth/*.h
    modplatform/packwiz/*.cpp
    modplatform/packwiz/*.h
    modplatform/technic/*.cpp
    modplatform/technic/*.h
)

add_library(auracore_core STATIC
    ${AURACORE_CORE_SOURCES}
    ${AURACORE_MINECRAFT_SOURCES}
    ${AURACORE_MODPLATFORM_SOURCES}
)
target_include_directories(auracore_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(auracore_core PUBLIC LAUNCHER_APPLICATION)

target_link_libraries(auracore_core PUBLIC
    BuildConfig
    Launcher_murmur2
    nbt++
    qdcss
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::NetworkAuth
    Qt6::Widgets
    Qt6::Xml
    ZLIB::ZLIB
)
if(TARGET PkgConfig::libarchive)
    target_link_libraries(auracore_core PUBLIC PkgConfig::libarchive)
elseif(TARGET LibArchive::LibArchive)
    target_link_libraries(auracore_core PUBLIC LibArchive::LibArchive)
else()
    target_link_libraries(auracore_core PUBLIC ${LIBARCHIVE_LIBRARIES})
    target_include_directories(auracore_core PUBLIC ${LIBARCHIVE_INCLUDE_DIRS})
endif()
if(TARGET PkgConfig::tomlplusplus)
    target_link_libraries(auracore_core PUBLIC PkgConfig::tomlplusplus)
else()
    target_link_libraries(auracore_core PUBLIC tomlplusplus::tomlplusplus)
endif()

if(WIN32)
    target_sources(auracore_core PRIVATE console/WindowsConsole.h console/WindowsConsole.cpp)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gamemode IMPORTED_TARGET gamemode)
    if(gamemode_FOUND)
        target_link_libraries(auracore_core PUBLIC PkgConfig::gamemode)
        target_compile_definitions(auracore_core PUBLIC AURACORE_WITH_GAMEMODE)
    endif()
endif()

