#[cfg(feature = "python")]
use pyo3::prelude::*;
#[cfg(feature = "python")]
use pyo3::wrap_pyfunction;

use std::ffi::{CStr, CString};
use std::io;
use std::os::raw::{c_char, c_int};
use std::ptr;
use std::sync::Mutex;

use once_cell::sync::Lazy;

pub mod g2p;
pub mod punc;
pub mod vi_normalizer;

pub fn punc_norm(text: &str) -> String {
    crate::punc::apply_punc_norm(text)
}

pub struct SeaPipeline {
    normalizer: vi_normalizer::Normalizer,
    engine: g2p::G2PEngine,
}

impl SeaPipeline {
    pub fn new(lang: &str, dict_path: &str) -> io::Result<Self> {
        Ok(Self {
            normalizer: vi_normalizer::Normalizer::new(lang),
            engine: g2p::G2PEngine::new(dict_path)?,
        })
    }

    pub fn normalize(&self, text: &str, punc_norm: bool) -> String {
        self.normalizer.normalize(text, punc_norm)
    }

    pub fn phonemize(&self, normalized_text: &str, punc_norm: bool) -> String {
        let input = if punc_norm {
            crate::punc::apply_punc_norm(normalized_text)
        } else {
            normalized_text.to_string()
        };
        self.engine.phonemize(&input)
    }

    pub fn run(&self, text: &str, punc_norm: bool) -> String {
        let normalized = self.normalize(text, punc_norm);
        self.engine.phonemize(&normalized)
    }
}

pub struct SeaG2pContext {
    pipeline: SeaPipeline,
}

static LAST_ERROR: Lazy<Mutex<Option<String>>> = Lazy::new(|| Mutex::new(None));

fn set_last_error(message: impl Into<String>) {
    if let Ok(mut slot) = LAST_ERROR.lock() {
        *slot = Some(message.into());
    }
}

fn cstr_to_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        set_last_error("received a null C string pointer");
        return None;
    }
    match unsafe { CStr::from_ptr(ptr).to_str() } {
        Ok(s) => Some(s.to_string()),
        Err(_) => {
            set_last_error("received a non-UTF-8 C string");
            None
        }
    }
}

fn string_to_c_ptr(value: String) -> *mut c_char {
    let sanitized = value.replace('\0', "");
    match CString::new(sanitized) {
        Ok(s) => s.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn sea_g2p_create(lang: *const c_char, dict_path: *const c_char) -> *mut SeaG2pContext {
    let lang = cstr_to_string(lang).unwrap_or_else(|| "vi".to_string());
    let Some(dict_path) = cstr_to_string(dict_path) else {
        return ptr::null_mut();
    };

    match SeaPipeline::new(&lang, &dict_path) {
        Ok(pipeline) => {
            set_last_error("");
            Box::into_raw(Box::new(SeaG2pContext { pipeline }))
        }
        Err(err) => {
            set_last_error(format!("failed to create sea-g2p context: {}", err));
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub extern "C" fn sea_g2p_destroy(ctx: *mut SeaG2pContext) {
    if !ctx.is_null() {
        unsafe {
            drop(Box::from_raw(ctx));
        }
    }
}

#[no_mangle]
pub extern "C" fn sea_g2p_normalize(
    ctx: *const SeaG2pContext,
    text: *const c_char,
    punc_norm: c_int,
) -> *mut c_char {
    if ctx.is_null() {
        set_last_error("sea_g2p_normalize received a null context");
        return ptr::null_mut();
    }
    let Some(text) = cstr_to_string(text) else {
        return ptr::null_mut();
    };

    let result = unsafe { &*ctx }.pipeline.normalize(&text, punc_norm != 0);
    string_to_c_ptr(result)
}

#[no_mangle]
pub extern "C" fn sea_g2p_phonemize(
    ctx: *const SeaG2pContext,
    normalized_text: *const c_char,
    punc_norm: c_int,
) -> *mut c_char {
    if ctx.is_null() {
        set_last_error("sea_g2p_phonemize received a null context");
        return ptr::null_mut();
    }
    let Some(normalized_text) = cstr_to_string(normalized_text) else {
        return ptr::null_mut();
    };

    let result = unsafe { &*ctx }.pipeline.phonemize(&normalized_text, punc_norm != 0);
    string_to_c_ptr(result)
}

#[no_mangle]
pub extern "C" fn sea_g2p_run(
    ctx: *const SeaG2pContext,
    text: *const c_char,
    punc_norm: c_int,
) -> *mut c_char {
    if ctx.is_null() {
        set_last_error("sea_g2p_run received a null context");
        return ptr::null_mut();
    }
    let Some(text) = cstr_to_string(text) else {
        return ptr::null_mut();
    };

    let result = unsafe { &*ctx }.pipeline.run(&text, punc_norm != 0);
    string_to_c_ptr(result)
}

#[no_mangle]
pub extern "C" fn sea_g2p_punc_norm(text: *const c_char) -> *mut c_char {
    let Some(text) = cstr_to_string(text) else {
        return ptr::null_mut();
    };
    string_to_c_ptr(crate::punc::apply_punc_norm(&text))
}

#[no_mangle]
pub extern "C" fn sea_g2p_free_string(value: *mut c_char) {
    if !value.is_null() {
        unsafe {
            drop(CString::from_raw(value));
        }
    }
}

#[no_mangle]
pub extern "C" fn sea_g2p_last_error() -> *mut c_char {
    let message = LAST_ERROR
        .lock()
        .ok()
        .and_then(|slot| slot.clone())
        .unwrap_or_default();
    string_to_c_ptr(message)
}

#[cfg(feature = "python")]
#[pyfunction(name = "punc_norm")]
fn py_punc_norm(text: &str) -> String {
    punc_norm(text)
}

#[cfg(feature = "python")]
#[pyclass]
struct G2P {
    engine: g2p::G2PEngine,
}

#[cfg(feature = "python")]
#[pymethods]
impl G2P {
    #[new]
    fn new(dict_path: &str) -> PyResult<Self> {
        let engine = g2p::G2PEngine::new(dict_path)
            .map_err(|e| pyo3::exceptions::PyIOError::new_err(e.to_string()))?;
        Ok(G2P { engine })
    }

    #[pyo3(signature = (text, punc_norm=false))]
    fn phonemize(&self, text: &str, punc_norm: bool) -> PyResult<String> {
        let input = if punc_norm { crate::punc::apply_punc_norm(text) } else { text.to_string() };
        Ok(self.engine.phonemize(&input))
    }

    #[pyo3(signature = (texts, punc_norm=false))]
    fn phonemize_batch(&self, py: Python<'_>, texts: Vec<String>, punc_norm: bool) -> PyResult<Vec<String>> {
        py.allow_threads(|| {
            use rayon::prelude::*;
            Ok(texts.into_par_iter().map(|t| {
                let input = if punc_norm { crate::punc::apply_punc_norm(&t) } else { t };
                self.engine.phonemize(&input)
            }).collect())
        })
    }
}

#[cfg(feature = "python")]
#[pymodule]
fn sea_g2p_rs(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_class::<G2P>()?;
    m.add_class::<vi_normalizer::Normalizer>()?;
    m.add_function(wrap_pyfunction!(py_punc_norm, m)?)?;
    Ok(())
}
