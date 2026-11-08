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

    var body: some View {
        GeometryReader { geo in
            let topInset = geo.safeAreaInsets.top
            let usable = CGSize(width: geo.size.width,
                                height: geo.size.height - topInset - geo.safeAreaInsets.bottom)
            let scale = min(usable.width / imageSize.width,
                            usable.height / imageSize.height)
            let drawn = CGSize(width: imageSize.width * scale, height: imageSize.height * scale)
            let origin = CGPoint(x: (geo.size.width - drawn.width) / 2,
                                 y: topInset + (usable.height - drawn.height) / 2)

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

                    PressZone(id: k.id, width: w, height: h, corner: min(w, h) * k.corner) {
                        if let cmd = k.cmd {
                            onCommand(cmd, k.id)
                        } else {
                            special(k.id)
                        }
                    }
                    .offset(x: x, y: y)
                    .accessibilityLabel(k.id)
                }
            }
            .frame(width: geo.size.width, height: geo.size.height, alignment: .topLeading)
            .background(Color.black)
        }
    }
}

enum RemoteArt {
    static let base = CGSize(width: 1220, height: 2548)      // Samsung recortada
    static let samsungCrop = CGPoint(x: 30, y: 40)
    static let androidBase = CGSize(width: 980, height: 1760) // Android recortado
    static let androidCrop = CGPoint(x: 150, y: 40)

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
    static let samsung: [HotKey] = samsungRaw.map { k in
        HotKey(id: k.id, box: k.box.offsetBy(dx: -samsungCrop.x, dy: -samsungCrop.y),
               corner: k.corner, cmd: k.cmd)
    }

    private static let samsungRaw: [HotKey] = {
        // centros e raios saíram do mesmo desenho que gerou a imagem
        func box(_ cx: CGFloat, _ cy: CGFloat, _ r: CGFloat) -> CGRect {
            CGRect(x: cx - r, y: cy - r, width: 2 * r, height: 2 * r)
        }
        var keys: [HotKey] = [
            HotKey(id: "power", box: box(252, 420, 144), cmd: .power),
            HotKey(id: "123", box: box(252, 860, 154)),
            HotKey(id: "extra", box: box(1280 - 252, 860, 154)),
            // roda: 4 bordas + OK
            HotKey(id: "up", box: box(640, 1460 - 226, 112), corner: 0.5, cmd: .up),
            HotKey(id: "down", box: box(640, 1460 + 226, 112), corner: 0.5, cmd: .down),
            HotKey(id: "left", box: box(640 - 226, 1460, 112), corner: 0.5, cmd: .left),
            HotKey(id: "right", box: box(640 + 226, 1460, 112), corner: 0.5, cmd: .right),
            HotKey(id: "ok", box: box(640, 1460, 168), corner: 0.5, cmd: .ok),
            // três teclas redondas
            HotKey(id: "voltar", box: box(252, 2012, 136), corner: 0.5, cmd: .back),
            HotKey(id: "home", box: box(640, 2012, 144), corner: 0.5, cmd: .home),
            HotKey(id: "play", box: box(1280 - 252, 2012, 136), corner: 0.5, cmd: .playPause),
            // balancins: metade de cima aumenta, de baixo diminui
            HotKey(id: "vol+", box: CGRect(x: 252 - 90, y: 2196, width: 180, height: 130),
                   corner: 0.45, cmd: .volUp),
            HotKey(id: "vol-", box: CGRect(x: 252 - 90, y: 2326, width: 180, height: 130),
                   corner: 0.45, cmd: .volDown),
            HotKey(id: "ch+", box: CGRect(x: 1280 - 252 - 90, y: 2196, width: 180, height: 130),
                   corner: 0.45, cmd: .chUp),
            HotKey(id: "ch-", box: CGRect(x: 1280 - 252 - 90, y: 2326, width: 180, height: 130),
                   corner: 0.45, cmd: .chDown),
            // mudo no vão entre os balancins
            HotKey(id: "mudo", box: box(640, 2326, 92), corner: 0.5, cmd: .mute)
        ]
        keys.append(contentsOf: [])
        return keys
    }()

    /// Camadas de toque do Android (TCL), nas mesmas coordenadas.
    static let android: [HotKey] = androidRaw.map { k in
        var m = k
        m = HotKey(id: k.id,
                   box: k.box.offsetBy(dx: -androidCrop.x, dy: -androidCrop.y),
                   corner: k.corner,
                   cmd: k.cmd)
        return m
    }

    private static let androidRaw: [HotKey] = [
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
        HotKey(id: "netflix", box: CGRect(x: 270, y: 1600, width: 358, height: 124)),
        HotKey(id: "globoplay", box: CGRect(x: 644, y: 1600, width: 366, height: 124))
    ]
}


/// Área de toque com afundamento e vibração: responde no encostar, não no soltar.
struct PressZone: View {
    let id: String
    let width: CGFloat
    let height: CGFloat
    let corner: CGFloat
    let action: () -> Void

    @GestureState private var down = false
    @State private var fired = false

    var body: some View {
        RoundedRectangle(cornerRadius: corner, style: .continuous)
            .fill(Color.white.opacity(down ? 0.26 : 0.001))
            .overlay(
                RoundedRectangle(cornerRadius: corner, style: .continuous)
                    .stroke(Color.white.opacity(down ? 0.70 : 0), lineWidth: down ? 2.5 : 0)
            )
            .overlay(
                RoundedRectangle(cornerRadius: corner, style: .continuous)
                    .stroke(Color.black.opacity(down ? 0.40 : 0), lineWidth: down ? 4 : 0)
                    .blur(radius: 3)
            )
            .frame(width: width, height: height)
            .scaleEffect(down ? 0.96 : 1)
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .updating($down) { _, state, _ in state = true }
                    .onChanged { _ in
                        guard !fired else { return }
                        fired = true
                        UIImpactFeedbackGenerator(style: .light).impactOccurred()
                        action()
                    }
                    .onEnded { _ in fired = false }
            )
            .animation(.easeOut(duration: 0.08), value: down)
    }
}
