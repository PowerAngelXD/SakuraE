use zed_extension_api as zed;

struct SakuraeExtension;

fn language_server_path(path_binary: Option<&str>, worktree_root: &str) -> String {
    path_binary
        .map(str::to_owned)
        .unwrap_or_else(|| format!("{worktree_root}/build/sakurae-language-server"))
}

impl zed::Extension for SakuraeExtension {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        language_server_id: &zed::LanguageServerId,
        worktree: &zed::Worktree,
    ) -> zed::Result<zed::Command> {
        if language_server_id.as_ref() != "sakurae-language-server" {
            return Err(format!("unsupported language server: {language_server_id}"));
        }

        let path_binary = worktree.which("sakurae-language-server");
        let binary = language_server_path(path_binary.as_deref(), &worktree.root_path());
        Ok(zed::Command::new(binary))
    }
}

zed::register_extension!(SakuraeExtension);

#[cfg(test)]
mod tests {
    use super::language_server_path;

    #[test]
    fn prefers_language_server_from_path() {
        assert_eq!(
            language_server_path(Some("/usr/bin/sakurae-language-server"), "/workspace"),
            "/usr/bin/sakurae-language-server"
        );
    }

    #[test]
    fn falls_back_to_workspace_build() {
        assert_eq!(
            language_server_path(None, "/workspace"),
            "/workspace/build/sakurae-language-server"
        );
    }
}
