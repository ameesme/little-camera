import SwiftUI

/// What the blog holds: the pictures with what people said under them, who reads it, and the way out.
struct BlogTab: View {
    let model: AppModel
    @State private var showingPeople = Demo.showingPeople
    @State private var entered = false
    @State private var scrollOffset: CGFloat = 0

    private var blog: Blog { model.blog }

    var body: some View {
        NavigationStack {
            Group {
                if blog.photos.isEmpty {
                    empty
                } else {
                    feed
                }
            }
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { showingPeople = true } label: {
                        Label("People", systemImage: "person.2.fill")
                    }
                }
                if let url = blog.bound.flatMap({ URL(string: $0.url) }) {
                    ToolbarItem(placement: .topBarTrailing) {
                        Button { UIApplication.shared.open(url) } label: {
                            Label("Open in Safari", systemImage: "globe.europe.africa.fill")
                        }
                    }
                }
            }
            .sheet(isPresented: $showingPeople) { PeopleScreen(blog: blog) }
        }
        .overlay(alignment: .topLeading) {
            blogHeader
                .padding(.top, 4)
                .padding(.leading, Theme.pad)
                .opacity(Double(max(0, 1 - scrollOffset / 30)))
        }
    }

    @ViewBuilder private var blogHeader: some View {
        if let bound = blog.bound {
            HStack(spacing: 10) {
                AsyncImage(url: URL(string: "\(bound.url)/avatar.png")) { image in
                    image.resizable().interpolation(.none)
                } placeholder: {
                    Dither()
                }
                .frame(width: 44, height: 44)
                .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
                VStack(alignment: .leading, spacing: 6) {
                    Caption("\(bound.name)'s little camera")
                        .lineLimit(1)
                    if let battery = model.camera.info?.batteryPercent {
                        HStack(spacing: 4) {
                            BatteryCell(fraction: Double(battery) / 100)
                            Text("\(battery) %").font(Theme.font(11))
                        }
                    }
                }
            }
        }
    }

    private var feed: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                Spacer().frame(height: 16)
                ForEach(Array(blog.photos.enumerated()), id: \.element.id) { index, photo in
                    VStack(alignment: .leading, spacing: 0) {
                        PhotoPost(photo: photo, blog: blog, first: index == 0)
                        Rule()
                    }
                    .opacity(entered ? 1 : 0)
                    .offset(y: entered ? 0 : 28)
                    // Capped, so a long blog does not spend a second and a half arriving.
                    .animation(.easeOut(duration: 0.5).delay(Double(min(index, 5)) * 0.07), value: entered)
                }
                if blog.nextPage != nil {
                    Button { blog.loadMore() } label: { Caption("Older") }
                        .buttonStyle(BlackButton())
                        .card()
                } else if !blog.photos.isEmpty {
                    Caption("That's all")
                        .font(Theme.font(10))
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 20)
                }
            }
        }
        .onScrollGeometryChange(for: CGFloat.self, of: { geo in geo.contentOffset.y + geo.contentInsets.top }, action: { _, new in scrollOffset = max(0, new) })
        .scrollDismissesKeyboard(.interactively)
        .onAppear { entered = true }
        .refreshable { model.refresh() }
        // The same ground every other screen stands on; a flat white page under the
        // pictures was the one place the app forgot it.
        .background(Ground(topOnly: true))
    }

    /// A blog with nothing on it is the camera waiting to be pressed, not an error.
    private var empty: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)
            Device(mood: .viewfinder)
            Text("Nothing here yet")
                .font(Theme.font(30, bold: true))
                .kerning(-1)
                .padding(.top, 28)
            Text("Time to take your first photo with little camera.")
                .font(Theme.lead)
                .multilineTextAlignment(.center)
                .padding(.horizontal, Theme.pad)
                .padding(.top, 10)
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Ground(topOnly: true))
    }
}

/// Battery icon: bordered body with dither fill and a nub on the right, matching the web blog header.
private struct BatteryCell: View {
    let fraction: Double

    var body: some View {
        HStack(spacing: 0) {
            ZStack(alignment: .leading) {
                Rectangle().fill(Color.clear).frame(width: 40, height: 14)
                Dither()
                    .frame(width: max(0, min(1, fraction)) * 36, height: 10)
                    .padding(2)
            }
            .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
            Rectangle().fill(Theme.ink).frame(width: 3, height: 6)
        }
    }
}

/// One picture, at the dither's own scale, with whatever was said under it.
private struct PhotoPost: View {
    let photo: FeedPhoto
    let blog: Blog
    var first = false
    @State private var confirmingDelete = false
    @State private var saying = false
    @State private var draft = ""
    @FocusState private var typing: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            AsyncImage(url: URL(string: photo.url)) { image in
                image
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(4.0 / 3.0, contentMode: .fit)
            } placeholder: {
                Dither().aspectRatio(4.0 / 3.0, contentMode: .fit)
            }
            .overlay(Rectangle().stroke(Theme.ink, lineWidth: 1))
            Caption(Date(timeIntervalSince1970: TimeInterval(photo.capturedAt)).formatted(date: .abbreviated, time: .shortened))
            ForEach(photo.comments) { comment in
                HStack(alignment: .firstTextBaseline, spacing: 10) {
                    Text("**\(comment.name)** \(comment.body)")
                        .font(Theme.small)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    Text(when(comment.createdAt))
                        .font(Theme.font(11))
                        .foregroundStyle(.secondary)
                        .fixedSize()
                }
            }
            if saying { say }
        }
        .task {
            saying = first && Demo.commenting
            confirmingDelete = first && Demo.deleting
        }
        .card()
        .contextMenu {
            Button { saying = true } label: { Label("Comment", systemImage: "text.bubble") }
            Button { save() } label: { Label("Save to photos", systemImage: "square.and.arrow.down") }
            Button(role: .destructive) { confirmingDelete = true } label: {
                Label { Text("Delete") } icon: { redTrash }
            }
        }
        .confirmationDialog("Delete this picture?", isPresented: $confirmingDelete, titleVisibility: .visible) {
            Button("Delete", role: .destructive) { blog.delete(photo) }
        } message: {
            Text("It goes off the blog for everyone, with whatever was said under it.")
        }
    }

    /// The menu bakes the inherited black into its symbols, so the red has to be baked in first.
    private var redTrash: Image {
        let icon = UIImage(systemName: "trash")?.withTintColor(.systemRed, renderingMode: .alwaysOriginal)
        return icon.map(Image.init(uiImage:)) ?? Image(systemName: "trash")
    }

    /// The owner says something under their own picture; it goes up under the blog's name.
    private var say: some View {
        HStack(spacing: 10) {
            TextField("Say something", text: $draft)
                .font(Theme.small)
                .textFieldStyle(.plain)
                .tint(Theme.ink)
                .submitLabel(.done)
                .focused($typing)
                .onSubmit { typing = false }
            Button(action: post) { Caption("Post") }
                .buttonStyle(SmallButton(height: 28))
                .disabled(draft.trimmed.isEmpty)
        }
        .padding(.top, 6)
        .task { typing = true }
        .onChange(of: typing) { _, now in
            if !now && draft.trimmed.isEmpty { saying = false }
        }
    }

    private func post() {
        let said = draft.trimmed
        guard !said.isEmpty else { return }
        draft = ""
        typing = false
        saying = false
        blog.comment(on: photo, body: said)
    }

    /// Recent enough to count in hours reads better than a date nobody has to look up.
    private func when(_ at: Int) -> String {
        let date = Date(timeIntervalSince1970: TimeInterval(at))
        guard date.timeIntervalSinceNow > -7 * 86400 else {
            return date.formatted(.dateTime.day().month(.abbreviated))
        }
        let since = RelativeDateTimeFormatter()
        since.unitsStyle = .abbreviated
        return since.localizedString(for: date, relativeTo: .now)
    }

    /// The server's PNG, not a redraw of it: the dither is the picture (AGENTS.md).
    private func save() {
        guard let url = URL(string: photo.url) else { return }
        Task {
            guard let (data, _) = try? await URLSession.shared.data(from: url),
                  let image = UIImage(data: data) else { return }
            UIImageWriteToSavedPhotosAlbum(image, nil, nil, nil)
        }
    }
}

