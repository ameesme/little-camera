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
        coordinator.pending = (mood, turn)
        coordinator.flush(web)
    }

    @MainActor
    final class Coordinator: NSObject, WKNavigationDelegate {
        var pending: (mood: DeviceMood, turn: Int)?
        private var sent: (mood: DeviceMood, turn: Int)?
        private var loaded = false

        func webView(_ web: WKWebView, didFinish navigation: WKNavigation!) {
            loaded = true
            sent = nil
            flush(web)
        }

        /// The page may not be up yet, so the last ask is kept and replayed when it is.
        func flush(_ web: WKWebView) {
            guard loaded, let pending else { return }
            guard sent == nil || sent! != pending else { return }
            web.evaluateJavaScript("window.lc.scene('\(pending.mood.rawValue)')")
            if sent?.turn != pending.turn && pending.turn > 0 {
                web.evaluateJavaScript("window.lc.spin()")
            }
            sent = pending
        }
    }
}
