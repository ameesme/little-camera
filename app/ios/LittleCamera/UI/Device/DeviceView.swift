import SwiftUI
import WebKit

/// Where the device is in the story. Each case is a screen on its panel and, sometimes, a turn.
enum DeviceMood: String {
    case asleep = "sleepA"
    case fine = "fineA"
    case glum = "glumA"
    case sad = "sadA"
    case viewfinder = "viewfinder"
    case gallery = "gallery"
    case saving = "save"
    case mail = "mail"
    case send = "send"
    case images = "images"
    case camera = "camera"
    case sparkles = "sparkles"
    case passkey = "passkey"
    case person = "person"
    case alert = "alert"
    case blog = "blog"
    case battery = "battery"
    /// What the panel itself says the moment a phone is let in (main.cpp, Sync::Event::PairingDone).
    case ohhi = "ohhi"
    case photo = "photo"

    /// The camera's own face, so the app never invents a mood of its own (docs/mood.md).
    init(_ face: Face) {
        switch face {
        case .content: self = .asleep
        case .fine: self = .fine
        case .glum: self = .glum
        case .sad: self = .sad
        }
    }
}

/// The camera itself, raymarched and dithered by the shader from lttl.cam (Resources/device.html).
/// Drag it to turn it; `mood` and `celebrating` are what the app tells it.
struct DeviceView: UIViewRepresentable {
    var mood: DeviceMood = .asleep
    /// Flipping this to a new value turns the camera all the way round once.
    var turn: Int = 0
    /// The camera turns into the phone it posts to, which scrolls the blog by itself.
    var phone: Bool = false
    var photo: String?

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeUIView(context: Context) -> WKWebView {
        let config = WKWebViewConfiguration()
        config.allowsInlineMediaPlayback = true
        let web = WKWebView(frame: .zero, configuration: config)
        web.navigationDelegate = context.coordinator
        web.isOpaque = false
        web.backgroundColor = .clear
        web.scrollView.backgroundColor = .clear
        web.scrollView.isScrollEnabled = false
        // A 3D object on a web page is still a web page; none of the furniture belongs here.
        web.scrollView.bounces = false
        if #available(iOS 16.4, *) { web.isInspectable = true }
        if let url = Bundle.main.url(forResource: "device", withExtension: "html") {
            web.loadFileURL(url, allowingReadAccessTo: url.deletingLastPathComponent())
        }
        return web
    }

    func updateUIView(_ web: WKWebView, context: Context) {
        let coordinator = context.coordinator
        coordinator.photo = photo
        coordinator.pending = (mood, turn, phone)
        coordinator.flush(web)
    }

    @MainActor
    final class Coordinator: NSObject, WKNavigationDelegate {
        var pending: (mood: DeviceMood, turn: Int, phone: Bool)?
        var photo: String?
        private var sent: (mood: DeviceMood, turn: Int, phone: Bool)?
        private var sentPhoto: String?
        private var loaded = false

        func webView(_ web: WKWebView, didFinish navigation: WKNavigation!) {
            loaded = true
            sent = nil
            sentPhoto = nil
            if let strip = Coordinator.strip {
                web.evaluateJavaScript("window.lc.strip('data:image/png;base64,\(strip)')")
            }
            flush(web)
        }

        /// Read once: every stage that shows the device builds one of these.
        private static let strip: String? = Bundle.main.url(forResource: "blog-strip", withExtension: "png")
            .flatMap { try? Data(contentsOf: $0) }?
            .base64EncodedString()

        /// The page may not be up yet, so the last ask is kept and replayed when it is.
        func flush(_ web: WKWebView) {
            guard loaded, let pending else { return }
            // A turn swaps the panel halfway round, so the picture has to be up before the scene.
            if let photo, photo != sentPhoto {
                sentPhoto = photo
                web.evaluateJavaScript("window.lc.photo('data:image/png;base64,\(photo)')")
            }
            guard sent == nil || sent! != pending else { return }
            // A morph turns the device on its own, and takes the panel with it halfway round.
            if sent?.phone != pending.phone {
                web.evaluateJavaScript("window.lc.morph(\(pending.phone), '\(pending.mood.rawValue)')")
            } else if sent?.turn != pending.turn && pending.turn > 0 {
                web.evaluateJavaScript("window.lc.turnTo('\(pending.mood.rawValue)')")
            } else {
                web.evaluateJavaScript("window.lc.scene('\(pending.mood.rawValue)')")
            }
            sent = pending
        }
    }
}
