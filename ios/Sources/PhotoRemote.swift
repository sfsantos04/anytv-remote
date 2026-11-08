import SwiftUI

// Controles renderizados (foto realista) usados como fundo em tela cheia.
// Cada tecla é uma área de toque invisível posicionada nas coordenadas da
// imagem; ao tocar, a tecla acende de leve e o aparelho vibra.

/// Tecla sobre a imagem: retângulo em coordenadas da imagem original.
struct HotKey: Identifiable {
    let id: String
    let box: CGRect          // em pixels da imagem base
    var corner: CGFloat = 0.25   // fração do menor lado, para o arredondado
    var cmd: Cmd? = nil      // nil = tecla especial, tratada pelo id
}

struct PhotoRemote: View {
    let imageName: String
    let imageSize: CGSize
    let keys: [HotKey]
    /// nil quando a tecla é especial (123, EXTRA, teclado, ajustes...).
    var special: (String) -> Void = { _ in }
    let onCommand: (Cmd, String) -> Void

    @State private var pressed: String?

    var body: some View {
        GeometryReader { geo in
            let scale = max(geo.size.width / imageSize.width,
                            geo.size.height / imageSize.height)
            let drawn = CGSize(width: imageSize.width * scale, height: imageSize.height * scale)
            let origin = CGPoint(x: (geo.size.width - drawn.width) / 2,
                                 y: (geo.size.height - drawn.height) / 2)

            ZStack(alignment: .topLeading) {
                RemoteArt.image(named: imageName)
                    .resizable()
                    .interpolation(.high)
                    .frame(width: drawn.width, height: drawn.height)
                    .offset(x: origin.x, y: origin.y)

                ForEach(keys) { k in
                    let w = k.box.width * scale
                    let h = k.box.height * scale
                    let x = origin.x + k.box.minX * scale
                    let y = origin.y + k.box.minY * scale

                    RoundedRectangle(cornerRadius: min(w, h) * k.corner, style: .continuous)
                        .fill(Color.white.opacity(pressed == k.id ? 0.20 : 0.001))
                        .overlay(
                            RoundedRectangle(cornerRadius: min(w, h) * k.corner, style: .continuous)
                                .stroke(Color.white.opacity(pressed == k.id ? 0.55 : 0), lineWidth: 2)
                        )
                        .frame(width: w, height: h)
                        .offset(x: x, y: y)
                        .contentShape(Rectangle())
                        .onTapGesture {
                            UIImpactFeedbackGenerator(style: .light).impactOccurred()
                            if let cmd = k.cmd {
                                onCommand(cmd, k.id)
                            } else {
                                special(k.id)
                            }
                            withAnimation(.easeOut(duration: 0.12)) { pressed = k.id }
                            DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) {
                                if pressed == k.id { withAnimation(.easeIn(duration: 0.15)) { pressed = nil } }
                            }
                        }
                        .accessibilityLabel(k.id)
                }
            }
            .frame(width: geo.size.width, height: geo.size.height, alignment: .topLeading)
            .clipped()
        }
        .ignoresSafeArea()
    }
}

enum RemoteArt {
    static let base = CGSize(width: 1280, height: 2660)   // render original (2x)

    /// Carrega a imagem do controle direto do bundle, com fallback pelo caminho
    /// do arquivo (Image(named:) falha quando o recurso vem solto no pacote).
    /// Existe no pacote? (decide entre imagem e controle desenhado)
    static func has(_ name: String) -> Bool {
        let plain = (name as NSString).deletingPathExtension
        let ext = (name as NSString).pathExtension.isEmpty ? "jpg" : (name as NSString).pathExtension
        if UIImage(named: name) != nil || UIImage(named: plain) != nil { return true }
        return Bundle.main.url(forResource: plain, withExtension: ext) != nil
    }

    static func image(named name: String) -> Image {
        let plain = (name as NSString).deletingPathExtension
        let ext = (name as NSString).pathExtension.isEmpty ? "jpg" : (name as NSString).pathExtension
        if let ui = UIImage(named: name) ?? UIImage(named: plain) {
            return Image(uiImage: ui)
        }
        if let url = Bundle.main.url(forResource: plain, withExtension: ext),
           let data = try? Data(contentsOf: url), let ui = UIImage(data: data) {
            Diagnostics.note("controle: imagem \(name) carregada por arquivo")
            return Image(uiImage: ui)
        }
        Diagnostics.note("controle: imagem \(name) NAO encontrada no pacote")
        return Image(systemName: "exclamationmark.triangle.fill")
    }

    /// Camadas de toque da Samsung, em coordenadas da imagem 1280×2660.
    static let samsung: [HotKey] = {
        // centros e raios saíram do mesmo desenho que gerou a imagem
        func box(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat) -> CGRect {
            CGRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r)
        }
        var keys: [HotKey] = [
            HotKey(id: "power", box: box(252, 424, 116), cmd: .power),
            HotKey(id: "123", box: box(252, 806, 128)),
            HotKey(id: "extra", box: box(1280 - 252, 806, 128)),
            // roda: 4 bordas + OK
            HotKey(id: "up", box: box(640, 1412 - 236, 104), corner: 0.5, cmd: .up),
            HotKey(id: "down", box: box(640, 1412 + 236, 104), corner: 0.5, cmd: .down),
            HotKey(id: "left", box: box(640 - 236, 1412, 104), corner: 0.5, cmd: .left),
            HotKey(id: "right", box: box(640 + 236, 1412, 104), corner: 0.5, cmd: .right),
            HotKey(id: "ok", box: box(640, 1412, 158), corner: 0.5, cmd: .ok),
            // três teclas redondas
            HotKey(id: "voltar", box: box(252, 1992, 112), corner: 0.5, cmd: .back),
            HotKey(id: "home", box: box(640, 1992, 120), corner: 0.5, cmd: .home),
            HotKey(id: "play", box: box(1280 - 252, 1992, 112), corner: 0.5, cmd: .playPause),
            // balancins: metade de cima aumenta, de baixo diminui
            HotKey(id: "vol+", box: CGRect(x: 252 - 76, y: 2150, width: 152, height: 143),
                   corner: 0.45, cmd: .volUp),
            HotKey(id: "vol-", box: CGRect(x: 252 - 76, y: 2293, width: 152, height: 143),
                   corner: 0.45, cmd: .volDown),
            HotKey(id: "ch+", box: CGRect(x: 1280 - 252 - 76, y: 2150, width: 152, height: 143),
                   corner: 0.45, cmd: .chUp),
            HotKey(id: "ch-", box: CGRect(x: 1280 - 252 - 76, y: 2293, width: 152, height: 143),
                   corner: 0.45, cmd: .chDown)
        ]
        keys.append(contentsOf: [])
        return keys
    }()

    /// Camadas de toque do Android (TCL), nas mesmas coordenadas.
    static let android: [HotKey] = [
        HotKey(id: "power", box: CGRect(x: 270, y: 230, width: 160, height: 150), cmd: .power),
        HotKey(id: "ajustes", box: CGRect(x: 850, y: 230, width: 160, height: 150), cmd: .settings),
        HotKey(id: "cima", box: CGRect(x: 552, y: 522, width: 176, height: 176), cmd: .up),
        HotKey(id: "esquerda", box: CGRect(x: 352, y: 712, width: 188, height: 176), cmd: .left),
        HotKey(id: "ok", box: CGRect(x: 552, y: 712, width: 176, height: 176), cmd: .ok),
        HotKey(id: "direita", box: CGRect(x: 740, y: 712, width: 188, height: 176), cmd: .right),
        HotKey(id: "baixo", box: CGRect(x: 552, y: 902, width: 176, height: 176), cmd: .down),
        HotKey(id: "home", box: CGRect(x: 270, y: 538, width: 160, height: 140), cmd: .home),
        HotKey(id: "lista", box: CGRect(x: 850, y: 538, width: 160, height: 140), cmd: .menu),
        HotKey(id: "voltar", box: CGRect(x: 270, y: 922, width: 160, height: 140), cmd: .back),
        HotKey(id: "entrada", box: CGRect(x: 850, y: 922, width: 160, height: 140), cmd: .input),
        HotKey(id: "assistente", box: CGRect(x: 270, y: 1108, width: 160, height: 144), cmd: .voice),
        HotKey(id: "vol+", box: CGRect(x: 560, y: 1108, width: 160, height: 164), corner: 0.45, cmd: .volUp),
        HotKey(id: "vol-", box: CGRect(x: 560, y: 1272, width: 160, height: 164), corner: 0.45, cmd: .volDown),
        HotKey(id: "ch+", box: CGRect(x: 850, y: 1100, width: 160, height: 130), cmd: .chUp),
        HotKey(id: "ch-", box: CGRect(x: 850, y: 1316, width: 160, height: 130), cmd: .chDown),
        HotKey(id: "teclado", box: CGRect(x: 270, y: 1318, width: 160, height: 140)),
        HotKey(id: "netflix", box: CGRect(x: 270, y: 1600, width: 370, height: 120)),
        HotKey(id: "globoplay", box: CGRect(x: 640, y: 1600, width: 370, height: 120))
    ]
}
