// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

#[cfg(windows)]
fn acquire_single_instance_mutex() -> Option<windows_sys::Win32::Foundation::HANDLE> {
    use windows_sys::Win32::Foundation::{GetLastError, ERROR_ALREADY_EXISTS};
    use windows_sys::Win32::System::Threading::CreateMutexW;

    let mutex_name: Vec<u16> = "Local\\Pmod_Single_Instance_Mutex\0".encode_utf16().collect();
    unsafe {
        let handle = CreateMutexW(std::ptr::null_mut(), 1, mutex_name.as_ptr());
        if handle.is_null() {
            return None;
        }
        if GetLastError() == ERROR_ALREADY_EXISTS {
            windows_sys::Win32::Foundation::CloseHandle(handle);
            return None;
        }
        Some(handle)
    }
}

#[cfg(windows)]
fn try_activate_existing_pmod_window() -> bool {
    use windows_sys::Win32::Foundation::{BOOL, HWND, LPARAM};
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        EnumWindows, GetWindowTextW, GetWindowThreadProcessId, IsWindowVisible, SetForegroundWindow,
        ShowWindow, SW_RESTORE, SW_SHOW,
    };

    struct SearchState {
        found_hwnd: HWND,
        my_pid: u32,
    }

    unsafe extern "system" fn enum_proc(hwnd: HWND, lparam: LPARAM) -> BOOL {
        let state = &mut *(lparam as *mut SearchState);
        if IsWindowVisible(hwnd) != 0 {
            let mut pid = 0u32;
            GetWindowThreadProcessId(hwnd, &mut pid);
            if pid != 0 && pid != state.my_pid {
                let mut title_buf = [0u16; 512];
                let len = GetWindowTextW(hwnd, title_buf.as_mut_ptr(), 512);
                if len > 0 {
                    let title = String::from_utf16_lossy(&title_buf[..len as usize]);
                    // Only match if it's the exact Pmod application window
                    if title == "Pmod" || title.starts_with("Pmod ") || title.contains("本地模型管理平台") {
                        state.found_hwnd = hwnd;
                        return 0; // stop enumerating
                    }
                }
            }
        }
        1 // continue
    }

    unsafe {
        let mut state = SearchState {
            found_hwnd: std::ptr::null_mut(),
            my_pid: std::process::id(),
        };
        EnumWindows(Some(enum_proc), &mut state as *mut SearchState as isize);
        if !state.found_hwnd.is_null() {
            ShowWindow(state.found_hwnd, SW_RESTORE);
            ShowWindow(state.found_hwnd, SW_SHOW);
            SetForegroundWindow(state.found_hwnd);
            return true;
        }
    }
    false
}

fn main() {
    std::panic::set_hook(Box::new(|info| {
        let err = format!(
            "PANIC: {}\nLocation: {:?}\nBacktrace: {:?}",
            info,
            info.location(),
            std::backtrace::Backtrace::capture()
        );
        if let Ok(exe) = std::env::current_exe() {
            if let Some(dir) = exe.parent() {
                let _ = std::fs::write(dir.join("panic.txt"), &err);
            }
        }
        let _ = std::fs::write("panic.txt", &err);
    }));

    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            let log_dir = dir.join("runtime").join("logs");
            let _ = std::fs::create_dir_all(&log_dir);
            let _ = std::fs::write(log_dir.join("desktop_startup.log"), "0. main started\n");
        }
    }

    #[cfg(windows)]
    let _instance_mutex = {
        let mutex_res = acquire_single_instance_mutex();
        if mutex_res.is_none() {
            let _ = try_activate_existing_pmod_window();
            return;
        }
        mutex_res
    };

    pmod_lib::run();
}
