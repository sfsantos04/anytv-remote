import Foundation
import SwiftUI

/// Rastro de passos gravado em arquivo. Sem Xcode não dá para ler o console do
/// iPhone, então cada chamada ao núcleo deixa uma linha aqui: se o app cair, o
/// próximo início mostra até onde chegou.
enum Diagnostics {
    private static let queue = DispatchQueue(label: "anytv.diagnostics")
    private static let limit = 60 * 1024

    static var fileURL: URL {
        let base = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        return base.appendingPathComponent("anytv.log")
    }

    static func note(_ message: String) {
        queue.async {
            let stamp = ISO8601DateFormatter().string(from: Date())
            let line = "[\(stamp)] \(message)\n"
            guard let data = line.data(using: .utf8) else { return }
            let url = fileURL
            if let handle = try? FileHandle(forWritingTo: url) {
                handle.seekToEndOfFile()
                handle.write(data)
                let size = handle.offsetInFile
                handle.closeFile()
                if size > UInt64(limit) { trim() }
            } else {
                try? data.write(to: url)
            }
        }
    }

    private static func trim() {
        guard let text = try? String(contentsOf: fileURL, encoding: .utf8) else { return }
        let keep = String(text.suffix(limit / 2))
        try? keep.write(to: fileURL, atomically: true, encoding: .utf8)
    }

    static func contents() -> String {
        (try? String(contentsOf: fileURL, encoding: .utf8)) ?? "(vazio)"
    }

    static func clear() {
        try? FileManager.default.removeItem(at: fileURL)
    }
}

struct DiagnosticsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var text = ""

    var body: some View {
        NavigationStack {
            ScrollView {
                Text(text)
                    .font(.system(size: 10, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(10)
                    .textSelection(.enabled)
            }
            .navigationTitle("Diagnóstico")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Fechar") { dismiss() }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Limpar") { Diagnostics.clear(); text = Diagnostics.contents() }
                }
            }
            .task { text = Diagnostics.contents() }
        }
    }
}
