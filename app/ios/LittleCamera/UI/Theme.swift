import CoreGraphics
import SwiftUI

// Black and white only: where a grey is unavoidable it is a 2 px checker, the same rule as the blog and the camera.

enum Theme {
    static let ink = Color.black
    static let paper = Color.white
    static let pad: CGFloat = 14

    /// Font.custom falls back to the system face by itself, so a missing Helvetica Neue needs no runtime check.
    static func font(_ size: CGFloat, bold: Bool = false) -> Font {
        Font.custom(bold ? "HelveticaNeue-Bold" : "HelveticaNeue", size: size)
    }

    static let caption = font(11, bold: true)
    static let body = font(14)
    /// What an instruction page says under its title, which is the only text on the page.
    static let lead = font(19)
    static let small = font(12)
}

/// Uppercase tracked label, the app's only heading style.
struct Caption: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        Text(text.uppercased())
            .font(Theme.caption)
            .kerning(1.2)
    }
}

/// 1 px black rule.
struct Rule: View {
    var body: some View {
        Theme.ink.frame(height: 1)
    }
}

/// 50 % grey without grey: a checker of 1 pt squares on a 2 pt grid.
struct Dither: View {
    var body: some View {
        Canvas { context, size in
            var path = Path()
            var row = 0
            var y: CGFloat = 0
            while y < size.height {
                var x: CGFloat = row % 2 == 0 ? 0 : 1
                while x < size.width {
                    path.addRect(CGRect(x: x, y: y, width: 1, height: 1))
                    x += 2
                }
                y += 1
                row += 1
            }
            context.fill(path, with: .color(Theme.ink))
        }
        .background(Theme.paper)
    }
}

/// Black fill, white uppercase text, inverts while pressed; outlined when disabled.
struct BlackButton: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        let plain = configuration.isPressed || !isEnabled
        return configuration.label
            .frame(maxWidth: .infinity, minHeight: 54)
            .foregroundStyle(plain ? Theme.ink : Theme.paper)
            .background {
                // Not answerable yet, so the fill goes to the checker rather than to
                // white, which would make it the button underneath it.
                if isEnabled { configuration.isPressed ? AnyView(Theme.paper) : AnyView(Theme.ink) } else { AnyView(Dither()) }
            }
            .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
    }
}

/// The second choice on a screen: the same size, outlined rather than filled.
struct QuietButton: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .frame(maxWidth: .infinity, minHeight: 54)
            .foregroundStyle(Theme.ink)
            .background(configuration.isPressed ? Theme.ink.opacity(0.08) : Theme.paper)
            .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
    }
}

/// The one question a screen asks, set at the size of the answer rather than of a form field.
struct Field: View {
    let placeholder: String
    @Binding var text: String
    var keyboard: UIKeyboardType = .default
    /// Set where the screen asks one question, so the keyboard is already up.
    var autofocus = false
    @FocusState private var focused: Bool

    var body: some View {
        VStack(spacing: 4) {
            TextField(placeholder, text: $text)
                .font(Theme.font(26, bold: true))
                .kerning(-0.5)
                .multilineTextAlignment(.center)
                .focused($focused)
                .task { focused = autofocus }
                .padding(.vertical, 6)
                .textFieldStyle(.plain)
                .keyboardType(keyboard)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .tint(Theme.ink)
            Theme.ink.frame(height: 2)
        }
    }
}

/// `LABEL ........ value` row used by the cards.
struct StatRow: View {
    let label: String
    let value: String

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Caption(label)
            Spacer(minLength: 8)
            Text(value)
                .font(Theme.body)
                .multilineTextAlignment(.trailing)
        }
    }
}

/// Card padding shared by every section.
struct CardPadding: ViewModifier {
    func body(content: Content) -> some View {
        content
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, Theme.pad)
            .padding(.vertical, 12)
    }
}

extension View {
    func card() -> some View {
        modifier(CardPadding())
    }
}

/// Progress bar: 1 px frame filled with the checker.
struct ProgressRule: View {
    let fraction: Double

    var body: some View {
        GeometryReader { geo in
            HStack(spacing: 0) {
                Dither().frame(width: max(0, min(1, fraction)) * geo.size.width)
                Spacer(minLength: 0)
            }
        }
        .frame(height: 8)
        .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
    }
}

/// A 1-bit photo, drawn without interpolation so the dither stays a dither.
struct Thumbnail: View {
    let image: CGImage

    var body: some View {
        Image(decorative: image, scale: 1)
            .resizable()
            .interpolation(.none)
            .aspectRatio(4.0 / 3.0, contentMode: .fit)
            .frame(width: 80)
            .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
    }
}

/// Shown wherever the app can do nothing until the person does something.
struct Waiting: View {
    let text: String

    var body: some View {
        HStack(spacing: 7) {
            Spinner()
            Text(text)
                .font(Theme.lead)
        }
        .padding(.top, 16)
    }
}

/// Eight blocks round a square, three lit at a time. The system spinner is a grey wheel,
/// and there is no grey here.
struct Spinner: View {
    private static let ring = [(0, 0), (1, 0), (2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (0, 1)]
    private static let block: CGFloat = 6
    private static let gap: CGFloat = 2
    private static let beat = 0.11

    var body: some View {
        let side = Spinner.block * 3 + Spinner.gap * 2
        TimelineView(.periodic(from: .now, by: Spinner.beat)) { timeline in
            let step = Int(timeline.date.timeIntervalSinceReferenceDate / Spinner.beat) % 8
            Canvas { context, _ in
                for (index, cell) in Spinner.ring.enumerated() where (step - index + 8) % 8 < 3 {
                    let x = CGFloat(cell.0) * (Spinner.block + Spinner.gap)
                    let y = CGFloat(cell.1) * (Spinner.block + Spinner.gap)
                    let square = CGRect(x: x, y: y, width: Spinner.block, height: Spinner.block)
                    context.fill(Path(square), with: .color(Theme.ink))
                }
            }
            .frame(width: side, height: side)
        }
        .frame(width: side, height: side)
        .accessibilityHidden(true)
    }
}
