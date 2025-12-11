public user result data (.csv file):

Time
FPS
1% High Frame Time
5% High Frame Time
GPU Utilization
GPU Usage
Memory Load
GPU Mem Used
GPU Mem Total
Frame Time Variance
Highest Frame Time
Frame Time
Memory Usage (MB)
Core <N> (%)




example user .csv file results data (with just 2 timesteps, usually will be about 100-200):

Time,FPS,Frame Time,Highest Frame Time,5% Highest Frame Time (Per-Second),GPU Render Time,CPU Render Time,Highest GPU Time,Highest CPU Time,Frame Time Variance,1% Low FPS (Cumulative),5% Low FPS (Cumulative),0.5% Low FPS (Cumulative),Display Width,Display Height,GPU Temp,GPU Usage,GPU Power,GPU Clock,GPU Mem Clock,GPU Fan,GPU Mem Used,GPU Mem Total,GPU SM Util,GPU Mem Bandwidth Util,GPU PCIe Rx,GPU PCIe Tx,GPU NVDEC Util,GPU NVENC Util,PDH_CPU_Usage(%),PDH_CPU_User_Time(%),PDH_CPU_Privileged_Time(%),PDH_CPU_Idle_Time(%),PDH_CPU_Freq(MHz),PDH_CPU_Interrupts/sec,PDH_CPU_DPC_Time(%),PDH_CPU_Interrupt_Time(%),PDH_CPU_DPCs_Queued/sec,PDH_CPU_DPC_Rate,PDH_CPU_C1_Time(%),PDH_CPU_C2_Time(%),PDH_CPU_C3_Time(%),PDH_CPU_C1_Transitions/sec,PDH_CPU_C2_Transitions/sec,PDH_CPU_C3_Transitions/sec,PDH_Memory_Available(MB),PDH_Memory_Load(%),PDH_Memory_Committed(bytes),PDH_Memory_Commit_Limit(bytes),PDH_Memory_Page_Faults/sec,PDH_Memory_Pages/sec,PDH_Memory_Pool_NonPaged(bytes),PDH_Memory_Pool_Paged(bytes),PDH_Memory_System_Code(bytes),PDH_Memory_System_Driver(bytes),PDH_Disk_Read_Rate(MB/s),PDH_Disk_Write_Rate(MB/s),PDH_Disk_Reads/sec,PDH_Disk_Writes/sec,PDH_Disk_Transfers/sec,PDH_Disk_Bytes/sec,PDH_Disk_Avg_Read_Queue,PDH_Disk_Avg_Write_Queue,PDH_Disk_Avg_Queue,PDH_Disk_Avg_Read_Time(sec),PDH_Disk_Avg_Write_Time(sec),PDH_Disk_Avg_Transfer_Time(sec),PDH_Disk_Percent_Time(%),PDH_Disk_Percent_Read_Time(%),PDH_Disk_Percent_Write_Time(%),PDH_Context_Switches/sec,PDH_System_Processor_Queue,PDH_System_Processes,PDH_System_Threads,PDH_System_Calls/sec,PDH_Core 0 CPU (%),PDH_Core 1 CPU (%),PDH_Core 2 CPU (%),PDH_Core 3 CPU (%),PDH_Core 4 CPU (%),PDH_Core 5 CPU (%),PDH_Core 6 CPU (%),PDH_Core 7 CPU (%),PDH_Core 0 Freq (MHz),PDH_Core 1 Freq (MHz),PDH_Core 2 Freq (MHz),PDH_Core 3 Freq (MHz),PDH_Core 4 Freq (MHz),PDH_Core 5 Freq (MHz),PDH_Core 6 Freq (MHz),PDH_Core 7 Freq (MHz),ETW_Interrupts/sec,ETW_DPCs/sec,ETW_Avg_DPC_Latency(μs),ETW_DPC_Latencies_>50μs(%),ETW_DPC_Latencies_>100μs(%),Disk_Read_Latency(ms),Disk_Write_Latency(ms),Disk_Queue_Length,Disk_Avg_Queue_Length,Disk_Max_Queue_Length,Disk_Min_Read_Latency(ms),Disk_Max_Read_Latency(ms),Disk_Min_Write_Latency(ms),Disk_Max_Write_Latency(ms),Disk_IO_Read_Total(MB),Disk_IO_Write_Total(MB)
283,155.42,6.43,86.62,7.16,2.05,4.39,2.73,83.89,9.53,13.80,139.70,11.54,1024,768,52,45,73,1950,7001,53,7108.3750,8192.0000,7,3,821550,173450,0,0,48.90,37.06,11.70,49.53,4123.79,60730.65,0.78,1.17,5217.55,82.00,49.53,0.00,0.00,47379.27,0.00,0.00,36740.00,43.86,39182045184.00,72921669632.00,10064.70,0.00,640147456.00,768000000.00,8192.00,109215744.00,0.00,0.02,0.00,3.99,3.99,16357.51,0.00,0.00,0.00,0.00,0.00,0.00,0.01,0.00,0.01,111923.00,0.00,230.00,3661.00,161423.57,42.27,53.19,46.95,39.15,46.95,50.07,51.63,60.99,4147,4110,4084,4059,4146,4109,4149,4174,402,5705,3.600,0.00,0.00,0.0000,0.0000,0.0000,1.0000,1.0000,0.0011,0.0224,0.0091,0.3735,1.57,0.51
284,206.80,4.84,10.09,5.63,1.99,2.85,2.79,7.97,0.67,135.70,177.67,99.15,1024,768,52,46,73,1950,7001,53,7108.3750,8192.0000,7,3,821550,173450,0,0,54.71,38.26,16.40,45.52,4136.35,63033.73,0.59,1.17,5662.18,81.00,45.52,0.00,0.00,45856.26,0.00,0.00,36842.00,43.71,39015776256.00,72921669632.00,11183.40,0.00,644898816.00,767938560.00,8192.00,109215744.00,0.00,0.04,0.00,6.00,6.00,40946.84,0.00,0.00,0.00,0.00,0.00,0.00,0.03,0.00,0.03,111870.00,0.00,230.00,3661.00,184089.82,48.47,57.84,50.03,45.34,60.96,59.40,64.08,51.59,4146,4137,4086,4097,4156,4132,4145,4171,388,5200,3.600,0.00,0.00,0.0000,0.0000,0.0000,1.0000,1.0000,0.0011,0.0374,0.0012,0.0128,1.41,0.16





example optimization settings:

{
    "metadata": {
        "cpu": "AMD Ryzen 7 5800X3D 8-Core Processor           ",
        "gpu": "NVIDIA GeForce RTX 3070",
        "os": "Windows 11",
        "ram_gb": 63.9130859375,
        "resolution": "3840x2160",
        "timestamp": "2025-07-14T21:44:57",
        "version": "1.0"
    },
    "nvidia": {
        "category": "nvidia",
        "settings": [
            {
                "id": "nvidia_vsync",
                "status": "ok",
                "value": 138504007
            },
            {
                "id": "nvidia_power_mode",
                "status": "ok",
                "value": 1
            },
            {
                "id": "nvidia_aniso_filtering",
                "status": "ok",
                "value": 1
            },
            {
                "id": "nvidia_antialiasing",
                "status": "ok",
                "value": 1
            },
            {
                "id": "nvidia_monitor_tech",
                "status": "ok",
                "value": 4
            },
            {
                "id": "nvidia_gdi_compat",
                "status": "ok",
                "value": 0
            },
            {
                "id": "nvidia_refresh_rate",
                "status": "ok",
                "value": 0
            },
            {
                "id": "nvidia_texture_quality",
                "status": "ok",
                "value": 20
            },
            {
                "id": "nvidia_aniso_sample_opt",
                "status": "ok",
                "value": 1
            },
            {
                "id": "nvidia_threaded_opt",
                "status": "ok",
                "value": 1
            }
        ],
        "timestamp": "2025-07-14T21:44:57"
    },
    "power_plan": {
        "category": "power_plan",
        "guid": "{121CB3D3-CFCA-4637-8385-B61B5050B7F5}",
        "status": "ok",
        "timestamp": "2025-07-14T21:44:57"
    },
    "registry": {
        "category": "registry",
        "settings": [
            {
                "id": "win.maintenance.disable",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Schedule\\Maintenance",
                "name": "MaintenanceDisabled",
                "status": "missing",
                "value": "__KEY_NOT_FOUND__"
            },
            {
                "id": "win.desktop.menudelay",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Desktop",
                "name": "MenuShowDelay",
                "status": "ok",
                "value": "0"
            },
            {
                "id": "win.system.responsiveness",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
                "name": "SystemResponsiveness",
                "status": "ok",
                "value": 20
            },
            {
                "id": "win.priority.separation",
                "key": "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
                "name": "Win32PrioritySeparation",
                "status": "ok",
                "value": 2
            },
            {
                "id": "win.power.fastboot",
                "key": "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power",
                "name": "HiberBootEnabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.sysmain.service",
                "key": "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\SysMain",
                "name": "Start",
                "status": "ok",
                "value": 2
            },
            {
                "id": "win.privacy.advertising.id",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo",
                "name": "Enabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.privacy.tailored.data",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Privacy",
                "name": "TailoredExperiencesWithDiagnosticDataEnabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.mouse.acceleration.wrapper",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Mouse",
                "name": "MouseSpeed",
                "status": "ok",
                "value": false
            },
            {
                "id": "win.accessibility.stickykeys",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Accessibility\\StickyKeys",
                "name": "Flags",
                "status": "ok",
                "value": 506
            },
            {
                "id": "win.gamebar.nexus",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\GameBar",
                "name": "UseNexusForGameBarEnabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.gamebar.gamemode",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\GameBar",
                "name": "AutoGameModeEnabled",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.gamedvr.enabled",
                "key": "HKEY_CURRENT_USER\\System\\GameConfigStore",
                "name": "GameDVR_Enabled",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.telemetry.allowtelemetry",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\DataCollection",
                "name": "AllowTelemetry",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.telemetry.cpss",
                "key": "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CPSS\\Store",
                "name": "AllowTelemetry",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.device.metadata",
                "key": "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Device Metadata",
                "name": "PreventDeviceMetadataFromNetwork",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.remote.assistance",
                "key": "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Remote Assistance",
                "name": "fAllowToGetHelp",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.graphics.hwscheduling",
                "key": "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
                "name": "HwSchMode",
                "status": "ok",
                "value": 2
            },
            {
                "id": "win.transparency",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                "name": "EnableTransparency",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.update.deliveryoptimization",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\DeliveryOptimization\\DeliveryOptimization\\Config",
                "name": "DODownloadMode",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.update.microsoftproducts",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\WindowsUpdate\\UX\\Settings",
                "name": "AllowMUUpdateService",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.update.getuptodate",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update",
                "name": "AUOptions",
                "status": "ok",
                "value": 3
            },
            {
                "id": "win.explorer.hidefileext",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                "name": "HideFileExt",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.explorer.cloudfiles.quickaccess",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer",
                "name": "ShowCloudFilesInQuickAccess",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.search.devicehistory",
                "key": "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SearchSettings",
                "name": "IsDeviceSearchHistoryEnabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.explorer.fullpath.titlebar",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CabinetState",
                "name": "FullPath",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.sharing.wizard",
                "key": "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                "name": "SharingWizardOn",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.explorer.network.namespace",
                "key": "HKEY_CURRENT_USER\\Software\\Classes\\CLSID\\{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}",
                "name": "System.IsPinnedToNameSpaceTree",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.audio.ducking.preference",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Multimedia\\Audio",
                "name": "UserDuckingPreference",
                "status": "ok",
                "value": 3
            },
            {
                "id": "win.notifications.account",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\SystemSettings\\AccountNotifications",
                "name": "EnableAccountNotifications",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.privacy.location.global",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\location",
                "name": "Value",
                "status": "ok",
                "value": "Deny"
            },
            {
                "id": "win.privacy.location.override",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\CPSS\\Store\\UserLocationOverridePrivacySetting",
                "name": "Value",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.privacy.voice.activation",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Speech_OneCore\\Settings\\VoiceActivation\\UserPreferenceForAllApps",
                "name": "AgentActivationEnabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.privacy.voice.activation.lastused",
                "key": "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Speech_OneCore\\Settings\\VoiceActivation\\UserPreferenceForAllApps",
                "name": "AgentActivationLastUsed",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.privacy.notifications.listener",
                "key": "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\userNotificationListener",
                "name": "Value",
                "status": "ok",
                "value": "Allow"
            },
            {
                "id": "win.privacy.language.list",
                "key": "HKEY_CURRENT_USER\\Control Panel\\International\\User Profile",
                "name": "HttpAcceptLanguageOptOut",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.privacy.mfu.tracking.machine",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\Windows\\EdgeUI",
                "name": "DisableMFUTracking",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.maps.autoupdate",
                "key": "HKEY_LOCAL_MACHINE\\SYSTEM\\Maps",
                "name": "AutoUpdateEnabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.apps.automatic.archiving",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\Windows\\Appx",
                "name": "AllowAutomaticAppArchiving",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.theme.dark.wrapper",
                "key": "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                "name": "AppsUseLightTheme",
                "status": "ok",
                "value": false
            },
            {
                "id": "win.gaming.tasks.priority",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
                "name": "Priority",
                "status": "ok",
                "value": 2
            },
            {
                "id": "win.gaming.tasks.gpu.priority",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
                "name": "GPU Priority",
                "status": "ok",
                "value": 8
            },
            {
                "id": "win.gaming.tasks.scheduling.category",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
                "name": "Scheduling Category",
                "status": "ok",
                "value": "High"
            },
            {
                "id": "win.gaming.tasks.sfio.priority",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
                "name": "SFIO Priority",
                "status": "ok",
                "value": "High"
            },
            {
                "id": "win.copilot.disable.wrapper",
                "key": "HKEY_CURRENT_USER\\Software\\Policies\\Microsoft\\Windows\\WindowsCopilot",
                "name": "TurnOffWindowsCopilot",
                "status": "ok",
                "value": true
            },
            {
                "id": "win.edge.performance.wrapper",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\Edge",
                "name": "StartupBoostEnabled",
                "status": "ok",
                "value": true
            },
            {
                "id": "win.privacy.tracking.disable.wrapper",
                "key": "HKEY_CURRENT_USER\\Software\\Policies\\Microsoft\\Windows\\EdgeUI",
                "name": "DisableMFUTracking",
                "status": "ok",
                "value": true
            },
            {
                "id": "win.taskbar.chat",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                "name": "TaskbarMn",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.taskbar.taskview",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                "name": "ShowTaskViewButton",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.taskbar.copilot",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                "name": "ShowCopilotButton",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.taskbar.meetnow",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
                "name": "HideSCAMeetNow",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.taskbar.newsinterests",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Feeds",
                "name": "EnableFeeds",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.start.recommendations",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                "name": "Start_IrisRecommendations",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.display.dpi.logpixels",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Desktop",
                "name": "LogPixels",
                "status": "missing",
                "value": "__KEY_NOT_FOUND__"
            },
            {
                "id": "win.display.dpi.win8scaling",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Desktop",
                "name": "Win8DpiScaling",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.display.dwm.dpiscaling",
                "key": "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\DWM",
                "name": "UseDpiScaling",
                "status": "missing",
                "value": "__KEY_NOT_FOUND__"
            },
            {
                "id": "win.display.dpi.perprocess",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Desktop",
                "name": "EnablePerProcessSystemDPI",
                "status": "missing",
                "value": "__KEY_NOT_FOUND__"
            },
            {
                "id": "win.store.autodownload",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\WindowsStore",
                "name": "AutoDownload",
                "status": "ok",
                "value": 2
            },
            {
                "id": "win.edge.background.mode",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\Edge",
                "name": "BackgroundModeEnabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.apps.background.execution",
                "key": "HKEY_LOCAL_MACHINE\\SOFTWARE\\Policies\\Microsoft\\Windows\\AppPrivacy",
                "name": "LetAppsRunInBackground",
                "status": "ok",
                "value": 2
            },
            {
                "id": "win.search.web.startmenu",
                "key": "HKEY_CURRENT_USER\\Software\\Policies\\Microsoft\\Windows\\Explorer",
                "name": "DisableSearchBoxSuggestions",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.wallpaper.master.control",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers",
                "name": "BackgroundType",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.wallpaper.background.type",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Wallpapers",
                "name": "BackgroundType",
                "status": "ok",
                "value": 1
            },
            {
                "id": "win.wallpaper.spotlight.enabled",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\DesktopSpotlight\\Settings",
                "name": "EnabledState",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.wallpaper.spotlight.content",
                "key": "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager",
                "name": "SubscribedContent-338389Enabled",
                "status": "ok",
                "value": 0
            },
            {
                "id": "win.wallpaper.style",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Desktop",
                "name": "WallpaperStyle",
                "status": "ok",
                "value": "10"
            },
            {
                "id": "win.wallpaper.tile",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Desktop",
                "name": "TileWallpaper",
                "status": "ok",
                "value": "0"
            },
            {
                "id": "win.wallpaper.path",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Desktop",
                "name": "Wallpaper",
                "status": "ok",
                "value": ""
            },
            {
                "id": "win.wallpaper.background.color",
                "key": "HKEY_CURRENT_USER\\Control Panel\\Colors",
                "name": "Background",
                "status": "ok",
                "value": "0 0 0"
            }
        ],
        "timestamp": "2025-07-14T21:44:57"
    },
    "rust": {
        "category": "rust",
        "settings": [
        ],
        "timestamp": "2025-07-14T21:44:57"
    },
    "stats": {
        "errors": 0,
        "exported": 77,
        "missing": 4,
        "total": 81
    },
    "visual_effects": {
        "category": "visual_effects",
        "profile_id": 3,
        "status": "ok",
        "timestamp": "2025-07-14T21:44:57"
    }
}



example specs file (in txt format for now, probably should be edited so that its saved as json in the future.):

STATUS: COMPLETE - Valid benchmark

STATUS: COMPLETE - Valid benchmark

Benchmark Information:
  Hash: 830cc2dc
  Timestamp: 2025-08-17 22:23:45
  User System ID: 04b94f088d701312893e0eeaadc0134a

CPU Information:
  Model: AMD Ryzen 7 5800X3D 8-Core Processor           
  Vendor: AuthenticAMD
  Architecture: Zen3/Zen3+
  Physical Cores: 8
  Logical Cores: 8
  Socket: AM4
  Base Clock: 3400 MHz
  Max Clock: 0 MHz
  L1 Cache: 512 KB
  L2 Cache: 4096 KB
  L3 Cache: 98304 KB
  Hyperthreading: Disabled
  Virtualization: Enabled
  AVX Support: Yes
  AVX2 Support: Yes

Memory Information:
  Total Physical: 63.9131 GB
  Total Physical (MB): 65447 MB
  Type: DDR4
  Clock: 3200 MHz
  XMP Enabled: Yes
  Channel Configuration: Dual Channel Mode

Memory Modules (2):
  Module 1:
    Capacity: 32.0000 GB
    Speed: 3200 MHz
    Configured Speed: 3200 MHz
    Manufacturer: Corsair
    Part Number: CMK64GX4M2E3200C16
    Type: DDR4
    Location: DIMM_A2
    Form Factor: 8
    Bank Label: BANK 1
  Module 2:
    Capacity: 32.0000 GB
    Speed: 3200 MHz
    Configured Speed: 3200 MHz
    Manufacturer: Corsair
    Part Number: CMK64GX4M2E3200C16
    Type: DDR4
    Location: DIMM_B2
    Form Factor: 8
    Bank Label: BANK 3

GPU Devices (1):
  GPU 1 (Primary):
    Model: NVIDIA GeForce RTX 3070
    Device ID: 0000
    Memory: 8 GB
    Memory (MB): 8192 MB
    Driver: 580.88
    Driver Date: 08/07/2025
    Has GeForce Experience: No
    Vendor: NVIDIA
    PCIe Width: 16
    PCIe Generation: 4
    Primary: Yes

Motherboard Information:
  Manufacturer: ASUSTeK COMPUTER INC.
  Model: ROG STRIX X570-F GAMING
  Chipset: AMD X570
  Chipset Driver Version: AMD Chipset Driver 7.04.09.545
  BIOS Version: 5031
  BIOS Date: 01/13/2025
  BIOS Manufacturer: System manufacturer

Storage Drives (4):
  Drive 1 (System Drive):
    Path: C:
    Model: Samsung SSD 980 PRO with Heatsink 2TB
    Interface: SCSI
    Capacity: 1862 GB
    Free Space: 1379 GB
    System Drive: Yes
    SSD: Yes
  Drive 2 (Data Drive):
    Path: E:
    Model: Samsung SSD 970 EVO Plus 500GB
    Interface: SCSI
    Capacity: 465 GB
    Free Space: 465 GB
    System Drive: No
    SSD: Yes
  Drive 3 (Data Drive):
    Path: F:
    Model: CT250BX100SSD1
    Interface: IDE
    Capacity: 232 GB
    Free Space: 232 GB
    System Drive: No
    SSD: Yes
  Drive 4 (Data Drive):
    Path: G:
    Model: ST1000DM010-2EP102
    Interface: IDE
    Capacity: 931 GB
    Free Space: 931 GB
    System Drive: No
    SSD: No

Power Settings:
  Power Plan: Ultimate Performance
  High Performance Power Plan: No
  Game Mode: Disabled

Page File Information:
  Exists: Yes
  System Managed: No
  Total Size: 4096.0000 MB
  Primary Drive: ?:
  Locations:
    ?:

OS Information:
  OS Version: Windows 11
  Build: 26100
  Windows 11: Yes
Monitor Information (2):
  Monitor 1 (Primary):
    Device Name: \\.\DISPLAY1
    Display Name: NVIDIA GeForce RTX 3070
    Resolution: 3840 x 2160
    Refresh Rate: 144 Hz
  Monitor 2 (Secondary):
    Device Name: \\.\DISPLAY2
    Display Name: NVIDIA GeForce RTX 3070
    Resolution: 1920 x 1080
    Refresh Rate: 60 Hz

Chipset Drivers (7):
  Driver #1: AMD Chipset Driver
    Version: 7.04.09.545
    Date: 8-20-2024
    Provider: Advanced Micro Devices, Inc.
  Driver #2: AMD PCI
    Version: 1.0.0.90
    Date: 3-26-2024
    Provider: Advanced Micro Devices
  Driver #3: AMD GPIO Controller
    Version: 2.2.0.134
    Date: 8-20-2024
    Provider: Advanced Micro Devices, Inc
  Driver #4: AMD SMBus
    Version: 5.12.0.44
    Date: 3-26-2024
    Provider: Advanced Micro Devices, Inc
  Driver #5: AMD PCI
    Version: 1.0.0.90
    Date: 3-26-2024
    Provider: Advanced Micro Devices
  Driver #6: AMD PCI
    Version: 1.0.0.90
    Date: 3-26-2024
    Provider: Advanced Micro Devices
  Driver #7: AMD PSP 11.0 Device
    Version: 5.39.0.0
    Date: 3-11-2025
    Provider: Advanced Micro Devices Inc.

Audio Drivers (2):
  Driver #1: Steinberg UR22C
    Version: 2.1.7.5
    Date: 4-26-2024
    Provider: Yamaha Corporation.
  Driver #2: High Definition Audio Device
    Version: 10.0.26100.3624
    Date: 3-22-2025
    Provider: Microsoft

Network Drivers (1):
  Driver #1: Intel(R) I211 Gigabit Network Connection
    Version: 13.0.14.0
    Date: 2-24-2022
    Provider: Intel

Rust Configuration:
  accessibility.allynametagcolour = 0
  accessibility.buildingblockedzonecolour = 0
  accessibility.clannametagcolour = 0
  accessibility.disablemovementininventory = False
  accessibility.enemynametagcolour = 0
  accessibility.healthbarcolour = 0
  accessibility.holosightcolour = 0
  accessibility.hungerbarcolour = 0
  accessibility.hydrationbarcolour = 0
  accessibility.ioarrowinputcolor = 0
  accessibility.ioarrowoutputcolor = 0
  accessibility.laserdetectorcolour = 0
  accessibility.mushroomcolour = 0
  accessibility.senduinavigationevents = False
  accessibility.teamnametagcolour = 0
  accessibility.treemarkercolor = 0
  audio.advancedocclusion = False
  audio.eventaudio = 0
  audio.game = 0.59
  audio.instruments = 0
  audio.master = 0.13
  audio.musicvolume = 0
  audio.musicvolumemenu = 0
  audio.speakers = 2
  audio.ui = 1
  audio.voiceprops = 0
  audio.voices = 1
  client.allowcameratiltondpv = True
  client.allowdiscordprovisionalaccount = True
  client.allowteaminvitesremoteplayers = True
  client.autosavepaintings = True
  client.bag_unclaim_duration = 2
  client.bagassignmode = 0
  client.building_guide_mode = 1
  client.buildingskin = 0
  client.buildingskinmetal = 0
  client.buildingskinstone = 0
  client.buildingskintoptier = 0
  client.buildingskinwood = 0
  client.cached_browser_print_tag_errors = False
  client.camdist = 2
  client.camfov = 70
  client.camoffset = (0.00, 1.00, 0.00)
  client.camoffset_relative = False
  client.clampscreenshake = True
  client.crosshair = True
  client.drawrangevolumes = True
  client.enablefriendslogging = False
  client.errortoasts_in_chat = False
  client.hascompletedtutorial = False
  client.hasdeclinedtutorial = True
  client.headbob = False
  client.hidedmsinstreamermode = False
  client.hitcross = True
  client.hurtpunch = False
  client.io_arrow_important_only = True
  client.io_arrow_mode = 1
  client.lookatradius = 0.2
  client.map_marker_autoname = True
  client.map_marker_color = -1
  client.orbitcamdist = 2
  client.orbitcamlookspeed = 50
  client.pushtotalk = True
  client.rockskin = 0
  client.selectedshippingcontainerblockcolour = 0
  client.showcaminfo = False
  client.showgrowableui = True
  client.showmissionprovidersonmap = True
  client.showsleepingbagsonmap = True
  client.showvendingmachinesonmap = True
  client.sortskinsrecentlyused = True
  client.torchskin = 0
  client.underwearskin = 0
  console.erroroverlay = True
  culling.entitymaxdist = 5000
  culling.entityminculldist = 15
  culling.entityminshadowculldist = 5
  culling.entityupdaterate = 5
  culling.env = True
  culling.envmindist = 10
  culling.safemode = False
  culling.toggle = True
  ddraw.hideddrawduringdemo = False
  debug.debugcamera_autoload = False
  debug.debugcamera_autosave = False
  debug.debugcamera_offset = (0.00, 0.00, 0.00)
  debug.debugcamera_preserve = False
  debug.invokeperformancetracking = False
  debug.showviewmodelaimhelper = False
  debug.showworldinfoinperformancereadout = False
  debug.viewmodelaimhelpwidth = 4
  decor.quality = 0
  demo.autodebugcam = False
  demo.compressshotkeyframes = False
  demo.showcommunityui = False
  demo.showlocalplayernametag = False
  demo.ui = True
  effects.antialiasing = 0
  effects.ao = False
  effects.bloom = False
  effects.creationeffects = False
  effects.hurtoverlay = True
  effects.hurtoverleyapplylighting = False
  effects.lensdirt = False
  effects.maxgibdist = 150
  effects.maxgiblife = 10
  effects.maxgibs = 0
  effects.mingiblife = 5
  effects.motionblur = False
  effects.otherplayerslightflares = True
  effects.shafts = False
  effects.sharpen = True
  effects.showoutlines = True
  effects.vignet = False
  espplayerinfo.blueteamid = 3000
  espplayerinfo.grayteamid = 7000
  espplayerinfo.greenteamid = 1000
  espplayerinfo.lavenderteamid = 9000
  espplayerinfo.mintteamid = 10000
  espplayerinfo.orangeteamid = 6000
  espplayerinfo.pinkteamid = 8000
  espplayerinfo.purpleteamid = 5000
  espplayerinfo.redteamid = 2000
  espplayerinfo.yellowteamid = 4000
  fps.limit = 0
  fps.limitinbackground = False
  fps.limitinmenu = True
  gametip.server_event_tips = True
  gametip.showgametips = True
  gc.buffer = 4096
  gesturecollection.showadmincinematicgesturesinbindings = False
  gesturecollection.slot0ring0bind = clap
  gesturecollection.slot10ring0bind = raiseroof
  gesturecollection.slot11ring0bind = cabbagepatch
  gesturecollection.slot12ring0bind = twist
  gesturecollection.slot1ring0bind = surrender
  gesturecollection.slot2ring0bind = hurry
  gesturecollection.slot3ring0bind = ok
  gesturecollection.slot4ring0bind = point
  gesturecollection.slot5ring0bind = shrug
  gesturecollection.slot6ring0bind = thumbsdown
  gesturecollection.slot7ring0bind = thumbsup
  gesturecollection.slot8ring0bind = victory
  gesturecollection.slot9ring0bind = wave
  global.aquaticvehicledismounttime = 0
  global.blockemoji = False
  global.blockemojianimations = False
  global.blockserveremoji = False
  global.censornudity = 2
  global.censorrecordings = False
  global.censorsigns = False
  global.consolescale = 12
  global.debuglanguage = 0
  global.flyingvehicledismounttime = 0
  global.god = True
  global.godforceoffoverlay = False
  global.groundvehicledismounttime = 0
  global.hideinteracttextwhileads = False
  global.hideteamleadermapmarkers = False
  global.horsedismounttime = 0
  global.language = en
  global.limitflashing = False
  global.perf = 6
  global.processmidiinput = False
  global.richpresence = False
  global.showblood = True
  global.showdeathmarkeroncompass = True
  global.showemojierrors = False
  global.showitemcountsonpickup = True
  global.signundobuffer = 10
  global.streamermode = False
  global.usesingleitempickupnotice = True
  graphics.af = 1
  graphics.aggressiveshadowlod = True
  graphics.aggressiveshadowlodwearable = True
  graphics.branding = True
  graphics.chat = True
  graphics.collapserenderers = True
  graphics.compass = 1
  graphics.contactshadows = False
  graphics.dlaa = False
  graphics.dlss = -1
  graphics.dof = False
  graphics.dof_aper = 12
  graphics.dof_barrel = 0
  graphics.dof_blur = 1
  graphics.dof_debug = False
  graphics.dof_focus_dist = 10
  graphics.dof_focus_time = 0.2
  graphics.dof_kernel_count = 0
  graphics.dof_mode = 0
  graphics.dof_squeeze = 0
  graphics.drawdistance = 809
  graphics.fov = 90
  graphics.grassshadows = False
  graphics.hlod = True
  graphics.impostorshadows = False
  graphics.lodbias = 5
  graphics.maxqueuedframes = 2
  graphics.parallax = 0
  graphics.reflexintervalus = 0
  graphics.reflexmode = 2
  graphics.renderscale = 1
  graphics.resolution = 22
  graphics.screenmode = 2
  graphics.shaderlod = 1
  graphics.shadowlights = 0
  graphics.shadowmode = 1
  graphics.shadowquality = 0
  graphics.uiscale = 0.9
  graphics.vclouds = 0
  graphics.viewmodeldepth = True
  graphics.vm_fov_scale = True
  graphics.vm_horizontal_flip = False
  graphics.vsync = 0
  graphicssettings.anisotropicfiltering = 0
  graphicssettings.billboardsfacecameraposition = False
  graphicssettings.enablelodcrossfade = True
  graphicssettings.globaltexturemipmaplimit = 3
  graphicssettings.particleraycastbudget = 4
  graphicssettings.pixellightcount = 0
  graphicssettings.shadowcascades = 1
  graphicssettings.shadowdistancepercent = 0
  graphicssettings.shadowmaskmode = 0
  graphicssettings.shadowresolution = 0
  graphicssettings.softparticles = False
  grass.displacement = False
  grass.distance = 100
  grass.quality = 0
  grass.refresh_budget = 0.3
  input.ads_sensitivity = 1
  input.alwayssprint = False
  input.autocrouch = False
  input.flipy = False
  input.holdtime = 0.2
  input.map_mode = 0
  input.radial_menu_mode = 0
  input.sensitivity = 1
  input.toggleads = False
  input.toggleduck = False
  input.vehicle_flipy = False
  input.vehicle_sensitivity = 1
  instruments.processsustainpedal = True
  inventory.quickcraftdelay = 0.75
  keyboardmidi.midikeymap = qwerty-uk.json
  legs.enablelegs = True
  lod.grid_refresh_budget = 0.1
  lookattooltip.crosshairmode = 0
  megaphone.ignorepushtotalk = True
  mesh.quality = 0
  metaldetectorsource.draweditorgizmos = False
  midiconvar.debugmode = False
  midiconvar.enabled = False
  nametags.enabled = False
  netgraph.enabled = False
  netgraph.updatespeed = 5
  particle.quality = 0
  party.party_invites_enabled = True
  player.cold_breath = True
  player.footik = True
  player.footikdistance = 30
  player.footikrate = 0.1
  player.noclipspeed = 25
  player.noclipspeedfast = 200
  player.noclipspeedslow = 2
  player.recoilcomp = True
  playercull.enabled = True
  playercull.maxplayerdist = 5000
  playercull.maxsleeperdist = 30
  playercull.minculldist = 20
  playercull.updaterate = 5
  playercull.visquality = 2
  projectile.preventcameraclip = True
  recordertool.debugrecording = False
  reflection.planarcount = 2
  reflection.planarreflections = True
  reflection.planarresolution = 1024
  render.instanced_rendering = 0
  render.instancing_render_distance = 500
  render.show_building_blocked = True
  rgbeffects.brightness = 1
  rgbeffects.colorcorrection_razer = (3.00, 3.00, 3.00)
  rgbeffects.colorcorrection_steelseries = (1.50, 1.50, 1.50)
  rgbeffects.enabled = True
  screenshot.hiresscreenshotcustomwidth = 0
  shoutcaststreamer.allowinternetstreams = False
  shoutcaststreamer.maxaudiostreams = 3
  socket_free_snappable.snappingmode = 2
  sss.enabled = True
  sss.halfres = True
  sss.quality = 0
  sss.scale = 1
  steam.use_steam_nicknames = True
  store.inventorymode = 0
  store.preloadweeklyskins = True
  strobelight.forceoff = False
  system.auto_cpu_affinity = True
  terrain.quality = 0
  texture.memory_budget_factor = 1.021701
  toolgun.classiceffects = False
  tree.meshes = 100
  tree.quality = 0
  ui.autoswitchchannel = True
  ui.scrollsensitivity = 1
  ui.showbeltbarbinds = False
  ui.showinventoryplayer = True
  ui.showusebind = False
  voice.loopback = False
  water.quality = 0
  water.reflections = 0


=== USER SYSTEM PROFILE ===
User System ID: 04b94f088d701312893e0eeaadc0134a
Profile Location: profiles/system_profile.json
