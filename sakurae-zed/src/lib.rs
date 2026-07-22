use zed_extension_api as zed;

struct SakuraeExtension;

impl zed::Extension for SakuraeExtension {
    fn new() -> Self {
        Self
    }
}

zed::register_extension!(SakuraeExtension);
