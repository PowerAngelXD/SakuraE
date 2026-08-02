import * as vscode from "vscode";
  import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
  } from "vscode-languageclient/node";

  let client: LanguageClient;

  export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration("sakurae");
    const serverPath = config.get<string>("languageServer.path");

    if (!serverPath) {
      vscode.window.showErrorMessage(
        "未配置 SakuraE Language Server 路径"
      );
      return;
    }

    const serverOptions: ServerOptions = {
      command: serverPath,
      args: [],
    };

    const clientOptions: LanguageClientOptions = {
      documentSelector: [
        {
          scheme: "file",
          language: "sak",
        },
      ],
    };

    client = new LanguageClient(
      "sakurae-language-server",
      "SakuraE Language Server",
      serverOptions,
      clientOptions
    );

    context.subscriptions.push(client.start());
  }

  export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
  }