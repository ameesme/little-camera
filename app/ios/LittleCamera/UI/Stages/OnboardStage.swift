import SwiftUI

/// No blog on this camera yet. Make one or sign in to one, let the camera photograph
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
                    turn: model.blog.isLinked ? 1 : 0
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
            next(to: .email, enabled: !name.trimmed.isEmpty)
            back(to: .choose)
        case .email:
            next(to: .handle, enabled: email.trimmed.contains("@"))
            back(to: .name)
        case .handle:
            Button { model.blog.createBlog(name: name, email: email, handle: handle) } label: {
                Caption("Make it")
            }
            .buttonStyle(BlackButton())
            .disabled(handle.trimmed.isEmpty)
            back(to: .email)
        case .signIn:
            Button { model.blog.signIn(email: email); step = .sent } label: { Caption("Email me a link") }
                .buttonStyle(BlackButton())
                .disabled(email.trimmed.isEmpty)
            back(to: .choose)
        case .sent:
            Button { model.refresh(); step = .bind } label: { Caption("I opened it") }
                .buttonStyle(BlackButton())
        case .bind:
            EmptyView()
        case .avatar:
            if !model.blog.awaitingAvatar {
                Button { model.blog.requestAvatar() } label: { Caption("Take it with the camera") }
                    .buttonStyle(BlackButton())
            }
            Button { model.refresh() } label: { Caption(model.blog.awaitingAvatar ? "Done" : "Skip") }
                .buttonStyle(QuietButton())
        }
    }

    private func next(to target: Step, enabled: Bool) -> some View {
        Button { step = target } label: { Caption("Next") }
            .buttonStyle(BlackButton())
            .disabled(!enabled)
    }

    private func back(to target: Step) -> some View {
        Button { step = target } label: { Caption("Back") }
            .buttonStyle(QuietButton())
    }

    /// The code fills the screen, because the camera has to read it off this glass.
    private var bindScreen: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)
            Text("Point the camera at this")
                .font(Theme.font(30, bold: true))
                .kerning(-1)
                .frame(maxWidth: .infinity)
                .padding(.horizontal, Theme.pad)
            if let code = model.blog.verificationCode {
                QRView(code: code)
                    .padding(.horizontal, Theme.pad)
                    .padding(.top, 24)
            } else {
                Dither().aspectRatio(1, contentMode: .fit).padding(.horizontal, Theme.pad)
            }
            Text("Holding the camera is the proof. Nothing is typed.")
                .font(Theme.lead)
                .padding(.top, 20)
            Waiting(text: "Waiting for the camera")
                .padding(.top, 10)
            Spacer(minLength: 0)
            Button { model.refresh() } label: { Caption("Check again") }
                .buttonStyle(QuietButton())
                .card()
        }
        .multilineTextAlignment(.center)
        .background(Theme.paper.ignoresSafeArea())
        .task { model.blog.fetchVerificationCode() }
    }
}
