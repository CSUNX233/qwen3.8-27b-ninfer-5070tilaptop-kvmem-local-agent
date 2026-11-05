#[cfg(windows)]
use std::os::windows::io::RawHandle;
use std::sync::Mutex;
#[cfg(windows)]
use windows_sys::Win32::Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE};
#[cfg(windows)]
use windows_sys::Win32::System::JobObjects::{
    AssignProcessToJobObject, CreateJobObjectW, SetInformationJobObject,
    JobObjectExtendedLimitInformation,
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION, JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
};

pub struct ProcessJobGroup {
    #[cfg(windows)]
    handle: Mutex<Option<HANDLE>>,
}

unsafe impl Send for ProcessJobGroup {}
unsafe impl Sync for ProcessJobGroup {}

impl ProcessJobGroup {
    pub fn new() -> Result<Self, String> {
        #[cfg(windows)]
        {
            let job = Self::create_job_handle()?;
            Ok(Self {
                handle: Mutex::new(Some(job)),
            })
        }
        #[cfg(not(windows))]
        {
            Ok(Self {})
        }
    }

    #[cfg(windows)]
    fn create_job_handle() -> Result<HANDLE, String> {
        unsafe {
            let job = CreateJobObjectW(std::ptr::null(), std::ptr::null());
            if job == std::ptr::null_mut() || job == INVALID_HANDLE_VALUE {
                return Err("Failed to create Windows Job Object".to_string());
            }

            let mut info: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

            let res = SetInformationJobObject(
                job,
                JobObjectExtendedLimitInformation,
                &info as *const _ as *const std::ffi::c_void,
                std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
            );

            if res == 0 {
                CloseHandle(job);
                return Err("Failed to set JobObjectExtendedLimitInformation".to_string());
            }

            Ok(job)
        }
    }

    pub fn reset(&self) -> Result<(), String> {
        self.terminate();
        #[cfg(windows)]
        {
            let new_job = Self::create_job_handle()?;
            if let Ok(mut lock) = self.handle.lock() {
                *lock = Some(new_job);
            }
        }
        Ok(())
    }

    #[cfg(windows)]
    pub fn assign_raw_handle(&self, raw_handle: RawHandle) -> Result<(), String> {
        unsafe {
            let mut lock = self.handle.lock().map_err(|e| e.to_string())?;
            if lock.is_none() {
                *lock = Some(Self::create_job_handle()?);
            }
            if let Some(job) = *lock {
                let ok = AssignProcessToJobObject(job, raw_handle as HANDLE);
                if ok == 0 {
                    return Err("Failed to assign process to Job Object".to_string());
                }
                Ok(())
            } else {
                Err("Job object unavailable".to_string())
            }
        }
    }

    #[cfg(not(windows))]
    pub fn assign_raw_handle(&self, _raw_handle: *mut std::ffi::c_void) -> Result<(), String> {
        Ok(())
    }

    pub fn terminate(&self) {
        #[cfg(windows)]
        unsafe {
            if let Ok(mut lock) = self.handle.lock() {
                if let Some(job) = lock.take() {
                    windows_sys::Win32::System::JobObjects::TerminateJobObject(job, 1);
                    CloseHandle(job);
                }
            }
        }
    }
}

impl Drop for ProcessJobGroup {
    fn drop(&mut self) {
        self.terminate();
    }
}

