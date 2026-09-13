import SwiftUI

struct BlogCard: View {
    let model: AppModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let bound = model.status?.bound {
                Caption("\(bound.name)'s little camera")
                if let url = URL(string: bound.url) {
                    Link(bound.url, destination: url)
                        .font(Theme.body)
                        .underline()
                }
                StatRow(label: "Photos", value: "\(bound.photoCount ?? 0)")
                StatRow(label: "Battery", value: bound.battery.map { "\($0) %" } ?? "—")
                Button {
                    model.requestAvatar()
                } label: {
                    Caption(model.avatarPending
                            ? "Next picture becomes your profile picture"
                            : "Take your profile picture")
                }
                .buttonStyle(BlackButton())
                .disabled(model.avatarPending)
            } else {
                Caption("Not linked yet")
                Text("Open \(model.serverBaseURL)/me on this phone and photograph the code. If it isn't recognised, type \(model.shortCode ?? "the code at the top") there.")
            }
            if let error = model.serverError {
                Text(error)
                    .font(Theme.small)
            }
        }
        .card()
    }
}

struct SubscribersSection: View {
    let model: AppModel
    @State private var showingAdd = false

    private var subscribers: [Subscriber] {
        model.status?.subscribers ?? []
    }

    private var waiting: [Subscriber] {
        subscribers.filter { $0.status == "pending" }
    }

    private var approved: [Subscriber] {
        subscribers.filter { $0.status == "approved" }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Caption("Subscribers")
                Spacer()
                Button {
                    showingAdd = true
                } label: {
                    Caption("Add by email")
                }
                .buttonStyle(BlackButton())
                .disabled(model.status?.bound == nil)
            }
            if !waiting.isEmpty {
                Caption("Waiting")
                ForEach(waiting) { s in
                    HStack(alignment: .firstTextBaseline, spacing: 8) {
                        SubscriberLabel(subscriber: s)
                        Spacer(minLength: 4)
                        Button {
                            model.approve(id: s.id)
                        } label: {
                            Caption("Approve")
                        }
                        .buttonStyle(BlackButton())
                        Button {
                            model.block(id: s.id)
                        } label: {
                            Caption("Block")
                        }
                        .buttonStyle(BlackButton())
                    }
                }
            }
            if approved.isEmpty {
                Text(model.status?.bound == nil ? "Subscribers appear once the camera is linked." : "Nobody yet.")
                    .font(Theme.small)
            } else {
                ForEach(approved) { s in
                    SubscriberLabel(subscriber: s)
                }
            }
        }
        .card()
        .sheet(isPresented: $showingAdd) {
            AddSubscriberSheet(model: model)
        }
    }
}

struct SubscriberLabel: View {
    let subscriber: Subscriber

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(subscriber.email)
                .lineLimit(1)
            if let name = subscriber.name, !name.isEmpty {
                Text(name)
                    .font(Theme.small)
            }
        }
    }
}

struct AddSubscriberSheet: View {
    let model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var email = ""
    @State private var name = ""

    private var isValid: Bool {
        email.contains("@") && email.contains(".")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Caption("Add by email")
            Text("They are approved right away and get a welcome link.")
                .font(Theme.small)
            Field(placeholder: "you@email.com", text: $email, keyboard: .emailAddress)
            Field(placeholder: "Name (optional)", text: $name)
            HStack {
                Button {
                    dismiss()
                } label: {
                    Caption("Cancel")
                }
                .buttonStyle(BlackButton())
                Spacer()
                Button {
                    model.addSubscriber(email: email, name: name.isEmpty ? nil : name)
                    dismiss()
                } label: {
                    Caption("Add")
                }
                .buttonStyle(BlackButton())
                .disabled(!isValid)
            }
            Spacer()
        }
        .padding(Theme.pad)
        .font(Theme.body)
        .foregroundStyle(Theme.ink)
        .tint(Theme.ink)
        .background(Theme.paper.ignoresSafeArea())
        .presentationDetents([.medium])
    }
}
