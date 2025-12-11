
ALL OF THE DATA HERE IS JUST EXAMPLE TEMPLATES. IN REALITY ALL OF THE COMMUNICATIONS MUST BE PROTOBUF FORMAT.

this is the client submission:

{
    "cpu": {
        "info": {
            "architecture": "Zen3/Zen3+",
            "avx2_support": true,
            "avx_support": true,
            "base_clock_mhz": 3400,
            "boost_summary": {
                "all_core_power_w": 0,
                "best_boosting_core": 5,
                "idle_power_w": 0,
                "max_boost_delta_mhz": 483,
                "single_core_power_w": 0
            },
            "cache_info": {
                "l1_kb": 512,
                "l2_kb": 4096,
                "l3_kb": 98304
            },
            "cold_start": {
                "avg_response_time_us": 636.59,
                "max_response_time_us": 752.9,
                "min_response_time_us": 572.3,
                "std_dev_us": 51.19954003699644,
                "variance_us": 2621.392900000001
            },
            "cores": -1,
            "cores_detail": [
                {
                    "boost_metrics": {
                        "all_core_clock_mhz": 4209,
                        "boost_delta_mhz": 438,
                        "idle_clock_mhz": 3926,
                        "single_load_clock_mhz": 4364
                    },
                    "clock_mhz": 0,
                    "core_number": 0,
                    "load_percent": 0
                },
                {
                    "boost_metrics": {
                        "all_core_clock_mhz": 4212,
                        "boost_delta_mhz": 477,
                        "idle_clock_mhz": 3879,
                        "single_load_clock_mhz": 4356
                    },
                    "clock_mhz": 0,
                    "core_number": 1,
                    "load_percent": 0
                },
                {
                    "boost_metrics": {
                        "all_core_clock_mhz": 4210,
                        "boost_delta_mhz": 346,
                        "idle_clock_mhz": 3942,
                        "single_load_clock_mhz": 4288
                    },
                    "clock_mhz": 0,
                    "core_number": 2,
                    "load_percent": 0
                },
                {
                    "boost_metrics": {
                        "all_core_clock_mhz": 4218,
                        "boost_delta_mhz": 196,
                        "idle_clock_mhz": 4126,
                        "single_load_clock_mhz": 4322
                    },
                    "clock_mhz": 0,
                    "core_number": 3,
                    "load_percent": 0
                },
                {
                    "boost_metrics": {
                        "all_core_clock_mhz": 4214,
                        "boost_delta_mhz": 396,
                        "idle_clock_mhz": 3881,
                        "single_load_clock_mhz": 4277
                    },
                    "clock_mhz": 4423,
                    "core_number": 4,
                    "load_percent": 0
                },
                {
                    "boost_metrics": {
                        "all_core_clock_mhz": 4210,
                        "boost_delta_mhz": 483,
                        "idle_clock_mhz": 3788,
                        "single_load_clock_mhz": 4271
                    },
                    "clock_mhz": 0,
                    "core_number": 5,
                    "load_percent": 0
                },
                {
                    "boost_metrics": {
                        "all_core_clock_mhz": 4215,
                        "boost_delta_mhz": 385,
                        "idle_clock_mhz": 3938,
                        "single_load_clock_mhz": 4323
                    },
                    "clock_mhz": 0,
                    "core_number": 6,
                    "load_percent": 0
                },
                {
                    "boost_metrics": {
                        "all_core_clock_mhz": 4209,
                        "boost_delta_mhz": 337,
                        "idle_clock_mhz": 3906,
                        "single_load_clock_mhz": 4243
                    },
                    "clock_mhz": 0,
                    "core_number": 7,
                    "load_percent": 0
                }
            ],
            "max_clock_mhz": 0,
            "model": "AMD Ryzen 7 5800X3D 8-Core Processor           ",
            "smt": "Disabled",
            "socket": "AM4",
            "threads": -1,
            "throttling": {
                "clock_drop_percent": 0,
                "detected": false,
                "detected_time_seconds": -1,
                "peak_clock": 0,
                "sustained_clock": 0
            },
            "vendor": "AuthenticAMD",
            "virtualization": "Enabled"
        },
        "results": {
            "avx": 370.2,
            "four_thread": 167,
            "game_sim_large": 9337802.281000989,
            "game_sim_medium": 12823340.531501818,
            "game_sim_small": 19201553.328858092,
            "multi_core": 167,
            "prime_time": 0,
            "raw_cache_latencies": [
                {
                    "latency": 1.177734375,
                    "size_kb": 4
                },
                {
                    "latency": 1.171875,
                    "size_kb": 8
                },
                {
                    "latency": 1.1767578125,
                    "size_kb": 16
                },
                {
                    "latency": 1.013427734375,
                    "size_kb": 32
                },
                {
                    "latency": 2.036376953125,
                    "size_kb": 64
                },
                {
                    "latency": 2.5391438802083335,
                    "size_kb": 96
                },
                {
                    "latency": 2.4354248046875,
                    "size_kb": 128
                },
                {
                    "latency": 2.7854817708333335,
                    "size_kb": 192
                },
                {
                    "latency": 3.046539306640625,
                    "size_kb": 256
                },
                {
                    "latency": 3.8088582356770835,
                    "size_kb": 384
                },
                {
                    "latency": 5.26458740234375,
                    "size_kb": 512
                },
                {
                    "latency": 7.803171793619792,
                    "size_kb": 768
                },
                {
                    "latency": 9.44731,
                    "size_kb": 1024
                },
                {
                    "latency": 11.79588,
                    "size_kb": 2048
                },
                {
                    "latency": 12.71282,
                    "size_kb": 3072
                },
                {
                    "latency": 13.53795,
                    "size_kb": 4096
                },
                {
                    "latency": 13.62026,
                    "size_kb": 6144
                },
                {
                    "latency": 14.16403,
                    "size_kb": 8192
                },
                {
                    "latency": 17.30545,
                    "size_kb": 12288
                },
                {
                    "latency": 19.27158,
                    "size_kb": 16384
                },
                {
                    "latency": 21.38644,
                    "size_kb": 24576
                },
                {
                    "latency": 24.24747,
                    "size_kb": 32768
                },
                {
                    "latency": 32.25502,
                    "size_kb": 49152
                },
                {
                    "latency": 42.45206,
                    "size_kb": 65536
                },
                {
                    "latency": 65.00743,
                    "size_kb": 131072
                },
                {
                    "latency": 91.50802,
                    "size_kb": 262144
                }
            ],
            "simd_scalar": 3534.4,
            "single_core": 172.43,
            "specific_cache_latencies": {
                "l1_ns": 2.4354248046875,
                "l2_ns": 10.621595,
                "l3_ns": 20.32901,
                "ram_ns": 78.257725
            }
        }
    },
    "drives": {
        "items": [
            {
                "info": {
                    "free_space_gb": 1352,
                    "interface_type": "SCSI",
                    "is_ssd": true,
                    "is_system_drive": true,
                    "model": "Samsung SSD 980 PRO with Heatsink 2TB",
                    "path": "C:\\",
                    "size_gb": 1862
                },
                "results": {
                    "access_time": 0.05249999999999999,
                    "iops_4k": 25930.906580797335,
                    "read_speed": 3563.841487012861,
                    "write_speed": 481.69901829481347
                }
            }
        ],
        "tested": true
    },
    "gpu": {
        "info": {
            "devices": [
                {
                    "device_id": "0000",
                    "driver_date": "08/07/2025",
                    "driver_version": "580.88",
                    "has_geforce_experience": false,
                    "is_primary": true,
                    "memory_mb": 8192,
                    "name": "NVIDIA GeForce RTX 3070",
                    "pci_link_width": 16,
                    "pcie_link_gen": 4,
                    "vendor": "NVIDIA"
                }
            ],
            "driver": "580.88",
            "memory_mb": 8192,
            "model": "NVIDIA GeForce RTX 3070"
        },
        "results": {
            "fps": 3495.297119140625,
            "frames": 34953,
            "render_time_ms": -1
        },
        "tested": true
    },
    "memory": {
        "info": {
            "available_memory_gb": 49.291839599609375,
            "channel_status": "Dual Channel Mode",
            "clock_speed_mhz": 3200,
            "modules": [
                {
                    "capacity_gb": 32,
                    "configured_clock_speed_mhz": 3200,
                    "device_locator": "DIMM_A2",
                    "manufacturer": "Corsair",
                    "memory_type": "DDR4",
                    "part_number": "CMK64GX4M2E3200C16",
                    "slot": 3,
                    "speed_mhz": 3200,
                    "xmp_status": "Running at rated speed"
                },
                {
                    "capacity_gb": 32,
                    "configured_clock_speed_mhz": 3200,
                    "device_locator": "DIMM_B2",
                    "manufacturer": "Corsair",
                    "memory_type": "DDR4",
                    "part_number": "CMK64GX4M2E3200C16",
                    "slot": 4,
                    "speed_mhz": 3200,
                    "xmp_status": "Running at rated speed"
                }
            ],
            "page_file": {
                "exists": true,
                "locations": [
                    {
                        "path": "?:"
                    }
                ],
                "primary_drive": "?:",
                "system_managed": false,
                "total_size_mb": 4096
            },
            "total_memory_gb": 63.9130859375,
            "type": "DDR4",
            "xmp_enabled": true
        },
        "results": {
            "bandwidth": 40369.95277030916,
            "latency": 94.30381333333332,
            "read_time": 0.6209716019750872,
            "stability_test": {
                "completed_loops": 3,
                "completed_patterns": 9,
                "error_count": 0,
                "passed": true,
                "test_performed": true,
                "tested_size_mb": 256
            },
            "write_time": 0.2190873260172772
        }
    },
    "metadata": {
        "combined_identifier": "04b94f088d701312893e0eeaadc0134a",
        "profile_last_updated": "2025-08-31T15:05:36",
        "run_as_admin": true,
        "system_hash": "5cff1c93888eb01cb11a459312b79bb2",
        "system_id": {
            "cpu": "no_data",
            "fingerprint": "no_data_ROG_STRIX_X570-F_GAMING_%3_NVIDIA_GeForce_RTX_3070",
            "gpu": "NVIDIA GeForce RTX 3070",
            "motherboard": "ROG STRIX X570-F GAMING"
        },
        "timestamp": "2025-08-31T15:52:58",
        "user_id": "cfed78e4-47aa-435d-b069-5c2426d8ced0",
        "version": "1.0"
    },
    "network": {
        "results": {
            "average_jitter_ms": 0.046666666666667356,
            "average_latency_ms": 79.4,
            "baseline_latency_ms": 6.1,
            "download_latency_ms": 6,
            "has_bufferbloat": false,
            "issues": "High latency detected. ",
            "packet_loss_percent": 0,
            "regional_latencies": [
                {
                    "latency_ms": 33,
                    "region": "EU (Germany)"
                },
                {
                    "latency_ms": 36,
                    "region": "EU (Paris)"
                },
                {
                    "latency_ms": 6.066666666666666,
                    "region": "EU (Sweden)"
                },
                {
                    "latency_ms": 1,
                    "region": "NEAR"
                },
                {
                    "latency_ms": 311,
                    "region": "Oceania"
                },
                {
                    "latency_ms": 150,
                    "region": "USA (Chicago)"
                },
                {
                    "latency_ms": 97.13333333333334,
                    "region": "USA (New York)"
                }
            ],
            "server_results": [
                {
                    "avg_latency_ms": 97.13333333333334,
                    "hostname": "206.71.50.230",
                    "ip_address": "206.71.50.230",
                    "jitter_ms": 0.24888888888889463,
                    "max_latency_ms": 99,
                    "min_latency_ms": 97,
                    "packet_loss_percent": 0,
                    "received_packets": 15,
                    "region": "USA (New York)",
                    "sent_packets": 20
                },
                {
                    "avg_latency_ms": 150,
                    "hostname": "209.142.68.29",
                    "ip_address": "209.142.68.29",
                    "jitter_ms": 0,
                    "max_latency_ms": 150,
                    "min_latency_ms": 150,
                    "packet_loss_percent": 0,
                    "received_packets": 15,
                    "region": "USA (Chicago)",
                    "sent_packets": 28
                },
                {
                    "avg_latency_ms": 1,
                    "hostname": "8.8.8.8",
                    "ip_address": "8.8.8.8",
                    "jitter_ms": 0,
                    "max_latency_ms": 1,
                    "min_latency_ms": 1,
                    "packet_loss_percent": 0,
                    "received_packets": 15,
                    "region": "NEAR",
                    "sent_packets": 15
                },
                {
                    "avg_latency_ms": 1,
                    "hostname": "1.1.1.1",
                    "ip_address": "1.1.1.1",
                    "jitter_ms": 0,
                    "max_latency_ms": 1,
                    "min_latency_ms": 1,
                    "packet_loss_percent": 0,
                    "received_packets": 15,
                    "region": "NEAR",
                    "sent_packets": 15
                },
                {
                    "avg_latency_ms": 33,
                    "hostname": "5.9.24.56",
                    "ip_address": "5.9.24.56",
                    "jitter_ms": 0,
                    "max_latency_ms": 33,
                    "min_latency_ms": 33,
                    "packet_loss_percent": 0,
                    "received_packets": 15,
                    "region": "EU (Germany)",
                    "sent_packets": 16
                },
                {
                    "avg_latency_ms": 36,
                    "hostname": "172.232.53.171",
                    "ip_address": "172.232.53.171",
                    "jitter_ms": 0,
                    "max_latency_ms": 36,
                    "min_latency_ms": 36,
                    "packet_loss_percent": 0,
                    "received_packets": 15,
                    "region": "EU (Paris)",
                    "sent_packets": 16
                },
                {
                    "avg_latency_ms": 6.066666666666666,
                    "hostname": "172.232.134.84",
                    "ip_address": "172.232.134.84",
                    "jitter_ms": 0.12444444444444423,
                    "max_latency_ms": 7,
                    "min_latency_ms": 6,
                    "packet_loss_percent": 0,
                    "received_packets": 15,
                    "region": "EU (Sweden)",
                    "sent_packets": 15
                },
                {
                    "avg_latency_ms": 311,
                    "hostname": "139.130.4.5",
                    "ip_address": "139.130.4.5",
                    "jitter_ms": 0,
                    "max_latency_ms": 311,
                    "min_latency_ms": 311,
                    "packet_loss_percent": 0,
                    "received_packets": 15,
                    "region": "Oceania",
                    "sent_packets": 17
                }
            ],
            "upload_latency_ms": 6.1
        },
        "tested": true
    },
    "system": {
        "info": {
            "audio_drivers": [
                {
                    "device_name": "Steinberg UR22C",
                    "driver_date": "4-26-2024",
                    "driver_version": "2.1.7.5",
                    "is_date_valid": true,
                    "provider_name": "Yamaha Corporation."
                },
                {
                    "device_name": "High Definition Audio Device",
                    "driver_date": "3-22-2025",
                    "driver_version": "10.0.26100.3624",
                    "is_date_valid": true,
                    "provider_name": "Microsoft"
                }
            ],
            "background": {
                "cpu_percentages": [
                    8.232168599235289,
                    1.7561352152559084,
                    1.3103759073512673,
                    1.263818405349561,
                    0.30417310279703774
                ],
                "gpu_percentages": [
                    5,
                    2.5,
                    2.5,
                    2.5,
                    2
                ],
                "has_dpc_latency_issues": false,
                "has_high_cpu_processes": false,
                "has_high_gpu_processes": false,
                "has_high_memory_processes": true,
                "max_process_cpu": 8.232168599235289,
                "max_process_memory_mb": 2101.12890625,
                "memory_metrics": {
                    "commit_limit_mb": 69543.5234375,
                    "commit_percent": 25.38436754590847,
                    "commit_total_mb": 17653.18359375,
                    "file_cache_mb": 9459.703125,
                    "kernel_nonpaged_mb": 537.0390625,
                    "kernel_paged_mb": 513.3515625,
                    "kernel_total_mb": 1050.390625,
                    "other_memory_mb": 5143.1875,
                    "physical_available_mb": 50518.50390625,
                    "physical_total_mb": 65447.5234375,
                    "physical_used_mb": 14929.01953125,
                    "physical_used_percent": 22.81067143129819,
                    "user_mode_private_mb": 8735.44140625
                },
                "memory_usages_mb": [
                    2101.12890625,
                    1418.18359375,
                    1268.3203125,
                    563.9609375,
                    412.8046875
                ],
                "summary": {
                    "has_background_issues": true,
                    "high_interrupt_activity": false,
                    "overall_impact": "significant"
                },
                "system_dpc_time": 0.4206895599046684,
                "system_interrupt_time": 0.4910519688168967,
                "total_cpu_usage": 21.95325694593114,
                "total_gpu_usage": 2
            },
            "bios": {
                "date": "01/13/2025",
                "manufacturer": "System manufacturer",
                "version": "5031"
            },
            "chipset_drivers": [
                {
                    "device_name": "AMD Chipset Driver",
                    "driver_date": "8-20-2024",
                    "driver_version": "7.04.09.545",
                    "is_date_valid": true,
                    "provider_name": "Advanced Micro Devices, Inc."
                },
                {
                    "device_name": "AMD PCI",
                    "driver_date": "3-26-2024",
                    "driver_version": "1.0.0.90",
                    "is_date_valid": true,
                    "provider_name": "Advanced Micro Devices"
                },
                {
                    "device_name": "AMD GPIO Controller",
                    "driver_date": "8-20-2024",
                    "driver_version": "2.2.0.134",
                    "is_date_valid": true,
                    "provider_name": "Advanced Micro Devices, Inc"
                },
                {
                    "device_name": "AMD SMBus",
                    "driver_date": "3-26-2024",
                    "driver_version": "5.12.0.44",
                    "is_date_valid": true,
                    "provider_name": "Advanced Micro Devices, Inc"
                },
                {
                    "device_name": "AMD PCI",
                    "driver_date": "3-26-2024",
                    "driver_version": "1.0.0.90",
                    "is_date_valid": true,
                    "provider_name": "Advanced Micro Devices"
                },
                {
                    "device_name": "AMD PCI",
                    "driver_date": "3-26-2024",
                    "driver_version": "1.0.0.90",
                    "is_date_valid": true,
                    "provider_name": "Advanced Micro Devices"
                },
                {
                    "device_name": "AMD PSP 11.0 Device",
                    "driver_date": "3-11-2025",
                    "driver_version": "5.39.0.0",
                    "is_date_valid": true,
                    "provider_name": "Advanced Micro Devices Inc."
                }
            ],
            "kernel_memory": {
                "note": "Kernel memory tracking removed - using ConstantSystemInfo for static memory data"
            },
            "monitors": [
                {
                    "device_name": "\\\\.\\DISPLAY1",
                    "display_name": "NVIDIA GeForce RTX 3070",
                    "height": 2160,
                    "is_primary": true,
                    "refresh_rate": 144,
                    "width": 3840
                },
                {
                    "device_name": "\\\\.\\DISPLAY2",
                    "display_name": "NVIDIA GeForce RTX 3070",
                    "height": 1080,
                    "is_primary": false,
                    "refresh_rate": 60,
                    "width": 1920
                }
            ],
            "motherboard": {
                "chipset": "AMD X570",
                "chipset_driver": "AMD Chipset Driver 7.04.09.545",
                "manufacturer": "ASUSTeK COMPUTER INC.",
                "model": "ROG STRIX X570-F GAMING"
            },
            "network_drivers": [
                {
                    "device_name": "Intel(R) I211 Gigabit Network Connection",
                    "driver_date": "2-24-2022",
                    "driver_version": "13.0.14.0",
                    "is_date_valid": true,
                    "provider_name": "Intel"
                }
            ],
            "os": {
                "build": "26100",
                "is_windows11": true,
                "version": "Windows 11"
            },
            "power": {
                "game_mode": false,
                "high_performance": false,
                "plan": "Ultimate Performance"
            },
            "virtualization": true
        }
    }
}



this is the "aggregate comparison data" we receive:

{
  "_id": "68b73765b75f3c9130a1f1dc",
  "category": "drive",
  "created_at": "2025-09-02T18:28:53.22Z",
  "data_format": "protobuf",
  "decoded_data": {
    "drive": {
      "tested": true,
      "date": "2025-09-02",
      "model": "Samsung SSD 980 PRO with Heatsink 2TB",
      "benchmarkResults": {
        "readSpeedMbS": 2531.2594640837474,
        "writeSpeedMbS": 356.4909453309255,
        "iops4k": 17426.508925533843,
        "accessTimeMs": 0.04564000000000001
      }
    }
  },
  "last_updated": "2025-09-02T22:10:24.943Z",
  "model_name": "Samsung SSD 980 PRO with Heatsink 2TB",
  "sample_count": 1,
  "sanitized_name": "samsung_ssd_980_pro_with_heatsink_2tb"
}

🔹 Document 2:
{
  "_id": "68b73765b75f3c9130a1f1dd",
  "category": "gpu",
  "created_at": "2025-09-02T18:28:53.22Z",
  "data_format": "protobuf",
  "decoded_data": {
    "gpu": {
      "date": "2025-09-02",
      "tested": true,
      "model": "NVIDIA GeForce RTX 3070",
      "fullModel": "NVIDIA GeForce RTX 3070",
      "benchmarkResults": {
        "fps": 3376.0423177083335,
        "frames": 34485
      }
    }
  },
  "last_updated": "2025-09-02T22:10:24.944Z",
  "model_name": "NVIDIA GeForce RTX 3070",
  "sample_count": 1,
  "sanitized_name": "nvidia_geforce_rtx_3070"
}

🔹 Document 3:
{
  "_id": "68b73765b75f3c9130a1f1de",
  "category": "memory",
  "created_at": "2025-09-02T18:28:53.22Z",
  "data_format": "protobuf",
  "decoded_data": {
    "memory": {
      "date": "2025-09-02",
      "type": "Corsair DDR4 3200MHz 64GB",
      "totalMemoryGb": 63.9130859375,
      "benchmarkResults": {
        "bandwidthMbS": 34342.02770476351,
        "latencyNs": 107.15581,
        "readTimeGbS": 0.57957314306767,
        "writeTimeGbS": 0.21401201155535274
      }
    }
  },
  "last_updated": "2025-09-02T22:10:24.943Z",
  "model_name": "Corsair DDR4 3200MHz 64GB",
  "sample_count": 1,
  "sanitized_name": "corsair_ddr4_3200mhz_64gb"
}

🔹 Document 4:
{
  "_id": "68b73765b75f3c9130a1f1df",
  "category": "cpu",
  "created_at": "2025-09-02T18:28:53.22Z",
  "data_format": "protobuf",
  "decoded_data": {
    "cpu": {
      "date": "2025-09-02",
      "model": "AMD Ryzen 7 5800X3D 8-Core Processor",
      "fullModel": "AMD Ryzen 7 5800X3D 8-Core Processor",
      "benchmarkResults": {
        "singleCoreMs": 169.08849999999998,
        "fourThreadMs": 168.5,
        "simdScalarUs": 3929.4374999999995,
        "avxUs": 373.4125,
        "gameSimSmallUps": 15632964.087978551,
        "gameSimMediumUps": 10082676.993645623,
        "gameSimLargeUps": 8070538.814580975
      },
      "cacheLatencies": [
        {
          "latency": 1.06005859375,
          "sizeKb": 4
        },
        {
          "latency": 1.068115234375,
          "sizeKb": 8
        },
        {
          "latency": 1.006591796875,
          "sizeKb": 16
        },
        {
          "latency": 0.9820556640625,
          "sizeKb": 32
        },
        {
          "latency": 2.161102294921875,
          "sizeKb": 64
        },
        {
          "latency": 2.4249267578125,
          "sizeKb": 96
        },
        {
          "latency": 2.7705535888671875,
          "sizeKb": 128
        },
        {
          "latency": 3.193613688151042,
          "sizeKb": 192
        },
        {
          "latency": 3.2592315673828125,
          "sizeKb": 256
        },
        {
          "latency": 4.3050486246744795,
          "sizeKb": 384
        },
        {
          "latency": 5.727184295654297,
          "sizeKb": 512
        },
        {
          "latency": 7.970853169759115,
          "sizeKb": 768
        },
        {
          "latency": 10.535309999999999,
          "sizeKb": 1024
        },
        {
          "latency": 13.154345,
          "sizeKb": 2048
        },
        {
          "latency": 13.76562,
          "sizeKb": 3072
        },
        {
          "latency": 14.470922499999999,
          "sizeKb": 4096
        },
        {
          "latency": 15.348734576822917,
          "sizeKb": 6144
        },
        {
          "latency": 15.85136,
          "sizeKb": 8192
        },
        {
          "latency": 20.1682925,
          "sizeKb": 12288
        },
        {
          "latency": 23.418715,
          "sizeKb": 16384
        },
        {
          "latency": 39.79265,
          "sizeKb": 24576
        },
        {
          "latency": 49.6498625,
          "sizeKb": 32768
        },
        {
          "latency": 58.590765,
          "sizeKb": 49152
        },
        {
          "latency": 75.72417,
          "sizeKb": 65536
        },
        {
          "latency": 97.03014999999999,
          "sizeKb": 131072
        },
        {
          "latency": 112.498085,
          "sizeKb": 262144
        }
      ]
    }
  },
  "last_updated": "2025-09-02T22:10:24.943Z",
  "model_name": "AMD Ryzen 7 5800X3D 8-Core Processor",
  "sample_count": 1,
  "sanitized_name": "amd_ryzen_7_5800x3d_8-core_processor"
}


