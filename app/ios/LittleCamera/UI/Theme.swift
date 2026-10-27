import SwiftUI

// Black and white only. No accent tints, no greys: where a grey is unavoidable
// it is a 2 px checker (`Dither`), the same rule as the blog and the camera.

enum Theme {
    static let ink = Color.black
    static let paper = Color.white
    static let pad: CGFloat = 14

    /// Helvetica Neue ships with iOS; Font.custom falls back to the system face
    /// by itself if the name is ever missing, so no runtime check is needed.
    static func font(_ size: CGFloat, bold: Bool = false) -> Font {
        Font.custom(bold ? "HelveticaNeue-Bold" : "HelveticaNeue", size: size)
    }

    static let caption = font(11, bold: true)
    static let body = font(14)
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
        let inverted = configuration.isPressed || !isEnabled
        return configuration.label
            .padding(.horizontal, 10)
            .padding(.vertical, 8)
            .foregroundStyle(inverted ? Theme.ink : Theme.paper)
            .background(inverted ? Theme.paper : Theme.ink)
            .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
    }
}

/// Underlined text input, like the blog's forms. 16 pt so iOS Safari-style
/// focus zoom never applies and it matches the web exactly.
struct Field: View {
    let placeholder: String
    @Binding var text: String
    var keyboard: UIKeyboardType = .default

    var body: some View {
        VStack(spacing: 4) {
            TextField(placeholder, text: $text)
                .font(Theme.font(16))
                .textFieldStyle(.plain)
                .keyboardType(keyboard)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .tint(Theme.ink)
            Rule()
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
