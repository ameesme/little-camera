import SwiftUI

/// No blog on this camera yet. Make one or sign in to one, let little camera photograph
/// the code on this screen, then let it take the picture that heads the blog.
struct OnboardStage: View {
    let model: AppModel

    /// One question per screen, in the order the server needs the answers.
    enum Step: Equatable {
        case choose
        case name
        case email
        case handle
        case signIn
        case sent
        case bind
        case avatar
    }

    @State private var step: Step = Demo.step ?? .choose
    @State private var name = ""
    @State private var email = ""
    @State private var handle = ""

    var body: some View {
        Group {
            if step == .bind {
                bindScreen
            } else {
                DeviceStage(
                    title: title,
                    mood: mood,
                    turn: model.blog.isLinked ? 1 : 0,
                    back: back
                ) {
                    content
                    if let error = model.blog.error {
                        Text(error).font(Theme.small)
                    }
                } actions: {
                    buttons
                }
            }
        }
        .onChange(of: model.blog.isLinked) { _, linked in
            if linked { step = .avatar }
        }
        .onChange(of: model.blog.made?.handle) { _, made in
            if made != nil { step = .bind }
        }
    }

    private var mood: DeviceMood {
        switch step {
        case .choose, .name, .email, .handle, .signIn: return .images
        case .sent: return .mail
        case .bind: return .images
        case .avatar: return .camera
        }
    }

    private var title: String {
        switch step {
        case .choose: return "Ready to blog?"
        case .name: return "What should we call you?"
        case .email: return "Your email"
        case .handle: return "Pick an address"
        case .signIn, .sent: return "Sign in"
        case .bind: return ""
        case .avatar: return "One of you"
        }
    }

    private var back: (() -> Void)? {
        switch step {
        case .choose, .avatar: return nil
        case .name, .signIn, .sent: return { step = .choose }
        case .email: return { step = .name }
        case .handle: return { step = .email }
        case .bind: return nil
        }
    }

    @ViewBuilder private var content: some View {
        switch step {
        case .choose:
            Text("Get started with a new blog\nor continue one you already have")
        case .name:
            Text("It goes under every picture you send.")
            Field(placeholder: "Your name", text: $name, autofocus: true)
        case .email:
            Text("The only way back in, so make it one you read.")
            Field(placeholder: "you@example.com", text: $email, keyboard: .emailAddress, autofocus: true)
        case .handle:
            Text("\(handle.isEmpty ? "yours" : handle).\(model.serverURL.withoutScheme)")
            Field(placeholder: "yours", text: $handle, autofocus: true)
        case .signIn:
            Text("The address your blog is registered to.")
            Field(placeholder: "you@example.com", text: $email, keyboard: .emailAddress, autofocus: true)
        case .sent:
            Text("Check your email. The link works for an hour.")
            Waiting(text: "Waiting for you")
        case .bind:
            EmptyView()
        case .avatar:
            Text(model.blog.awaitingAvatar
                 ? "Take one and press send."
                 : "Linked. Now a picture to head the blog.")
            if model.blog.awaitingAvatar {
                Waiting(text: "Waiting for the picture")
            }
        }
    }

    @ViewBuilder private var buttons: some View {
        switch step {
        case .choose:
            Button { step = .name } label: { Caption("Make a blog") }
                .buttonStyle(BlackButton())
            Button { step = .signIn } label: { Caption("I already have one") }
                .buttonStyle(QuietButton())
        case .name:
            primary("Next", enabled: !name.trimmed.isEmpty) { step = .email }
        case .email:
            primary("Next", enabled: email.trimmed.contains("@")) { step = .handle }
        case .handle:
            primary("Make it", enabled: !handle.trimmed.isEmpty) {
                model.blog.createBlog(name: name, email: email, handle: handle)
            }
        case .signIn:
            primary("Email me a link", enabled: email.trimmed.contains("@")) {
                model.blog.signIn(email: email)
                step = .sent
            }
        case .sent:
            primary("Check again") { model.refresh(); step = .bind }
        case .bind:
            EmptyView()
        case .avatar:
            if model.blog.awaitingAvatar {
                primary("Done") { model.skipAvatar() }
            } else {
                primary("Take it with little camera") { model.blog.requestAvatar() }
            }
        }
    }

    private func primary(_ label: String, enabled: Bool = true, action: @escaping () -> Void) -> some View {
        Button(action: action) { Caption(label) }
            .buttonStyle(BlackButton())
            .disabled(!enabled)
    }

    /// The code as little camera has to see it: a card inside its own 4:3 frame, held up to it.
    private var bindScreen: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)
            Text("Point little camera at this")
                .font(Theme.font(30, bold: true))
                .kerning(-1)
                .frame(maxWidth: .infinity)
                .padding(.horizontal, Theme.pad)
            ScanCard(code: model.blog.verificationCode, name: cardName)
                .padding(.horizontal, Theme.pad)
                .padding(.top, 28)
            Text("Holding little camera is the proof. Nothing is typed.")
                .font(Theme.lead)
                .padding(.horizontal, Theme.pad)
                .padding(.top, 24)
            Waiting(text: "Waiting for little camera")
                .padding(.top, 10)
            Spacer(minLength: 0)
            Button { model.refresh() } label: { Caption("Check again") }
                .buttonStyle(BlackButton())
                .card()
        }
        .multilineTextAlignment(.center)
        .background(Theme.paper.ignoresSafeArea())
        .task { model.blog.fetchVerificationCode() }
    }

    private var cardName: String {
        model.blog.made?.handle ?? model.blog.bound?.handle ?? handle.trimmed
    }
}

/// A 4:3 card, the shape of what little camera sees, with the corners marked so it is
/// obvious where to aim. The name under the code says which blog is being joined.
private struct ScanCard: View {
    let code: String?
    let name: String

    var body: some View {
        VStack(spacing: 14) {
            Group {
                if let code {
                    QRView(code: code)
                } else {
                    Dither().aspectRatio(1, contentMode: .fit)
                }
            }
            .frame(maxHeight: .infinity)
            if !name.isEmpty {
                Text(name)
                    .font(Theme.font(19, bold: true))
                    .kerning(-0.3)
            }
        }
        .padding(22)
        .frame(maxWidth: .infinity)
        .aspectRatio(4.0 / 3.0, contentMode: .fit)
        .overlay(Corners().stroke(Theme.ink, lineWidth: 2))
    }
}

/// Four crop marks, the way a viewfinder draws them.
private struct Corners: Shape {
    var length: CGFloat = 26

    func path(in rect: CGRect) -> Path {
        var path = Path()
        for x in [rect.minX, rect.maxX] {
            for y in [rect.minY, rect.maxY] {
                let dx: CGFloat = x == rect.minX ? length : -length
                let dy: CGFloat = y == rect.minY ? length : -length
                path.move(to: CGPoint(x: x + dx, y: y))
                path.addLine(to: CGPoint(x: x, y: y))
                path.addLine(to: CGPoint(x: x, y: y + dy))
            }
        }
        return path
    }
}
