#include <bits/stdc++.h>
#include <unistd.h>
#include <png.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

struct Color {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;

    static Color rgba(int rr, int gg, int bb, int aa = 255) {
        Color c;
        c.r = static_cast<unsigned char>(rr);
        c.g = static_cast<unsigned char>(gg);
        c.b = static_cast<unsigned char>(bb);
        c.a = static_cast<unsigned char>(aa);
        return c;
    }

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator!=(const Color& other) const {
        return !(*this == other);
    }

    int to_rgb_hex() const {
        return (static_cast<int>(r) << 16) | (static_cast<int>(g) << 8) | static_cast<int>(b);
    }
};

static int clamp_int(int value, int low, int high) {
    if (low > high) return low;
    return max(low, min(value, high));
}

static string trim_copy(string value) {
    while (!value.empty() && isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

static bool path_exists(const string& path) {
    return access(path.c_str(), F_OK) == 0;
}

static bool is_dir(const string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static string file_stem_name(const string& path) {
    size_t slash = path.find_last_of('/');
    string name = slash == string::npos ? path : path.substr(slash + 1);
    return name.empty() ? path : name;
}

static string dirname_of(const string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

static string join_path(const string& a, const string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

static string now_string() {
    time_t now = time(nullptr);
    tm local_tm {};
    localtime_r(&now, &local_tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &local_tm);
    return string(buf);
}

static unsigned long pack_rgb(const Color& color) {
    return (static_cast<unsigned long>(color.r) << 16) |
           (static_cast<unsigned long>(color.g) << 8) |
           static_cast<unsigned long>(color.b);
}

static Color blend_over(const Color& base, const Color& top) {
    if (top.a == 255) return top;
    if (top.a == 0) return base;
    Color out = base;
    int src_alpha = static_cast<int>(top.a);
    int inv_alpha = 255 - src_alpha;
    out.r = static_cast<unsigned char>((top.r * src_alpha + base.r * inv_alpha) / 255);
    out.g = static_cast<unsigned char>((top.g * src_alpha + base.g * inv_alpha) / 255);
    out.b = static_cast<unsigned char>((top.b * src_alpha + base.b * inv_alpha) / 255);
    out.a = static_cast<unsigned char>(min(255, src_alpha + static_cast<int>(base.a) * inv_alpha / 255));
    return out;
}

class Image {
public:
    int w = 0;
    int h = 0;
    vector<Color> pixels;

    void init(int width, int height, const Color& fill = Color::rgba(0, 0, 0, 0)) {
        w = max(1, width);
        h = max(1, height);
        pixels.assign(w * h, fill);
    }

    bool in_bounds(int row, int col) const {
        return row >= 0 && row < h && col >= 0 && col < w;
    }

    Color get(int row, int col) const {
        if (!in_bounds(row, col)) return Color::rgba(0, 0, 0, 0);
        return pixels[row * w + col];
    }

    Color& at(int row, int col) {
        return pixels[row * w + col];
    }

    void set(int row, int col, const Color& color) {
        if (!in_bounds(row, col)) return;
        pixels[row * w + col] = color;
    }

    void clear(const Color& color) {
        fill(pixels.begin(), pixels.end(), color);
    }

    void resize_keep(int new_w, int new_h, const Color& fill) {
        new_w = max(1, new_w);
        new_h = max(1, new_h);
        vector<Color> next(new_w * new_h, fill);
        int copy_w = min(w, new_w);
        int copy_h = min(h, new_h);
        for (int row = 0; row < copy_h; ++row) {
            for (int col = 0; col < copy_w; ++col) {
                next[row * new_w + col] = pixels[row * w + col];
            }
        }
        w = new_w;
        h = new_h;
        pixels.swap(next);
    }
};

static bool read_png(const string& filename, Image& image) {
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp) return false;

    png_byte header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        fclose(fp);
        return false;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        fclose(fp);
        return false;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    int width = static_cast<int>(png_get_image_width(png_ptr, info_ptr));
    int height = static_cast<int>(png_get_image_height(png_ptr, info_ptr));
    int color_type = png_get_color_type(png_ptr, info_ptr);
    int bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);

    png_read_update_info(png_ptr, info_ptr);
    int channels = static_cast<int>(png_get_channels(png_ptr, info_ptr));
    image.init(width, height);

    vector<png_bytep> rows(height, nullptr);
    vector<vector<unsigned char>> storage(height);
    for (int row = 0; row < height; ++row) {
        storage[row].resize(png_get_rowbytes(png_ptr, info_ptr));
        rows[row] = storage[row].data();
    }

    png_read_image(png_ptr, rows.data());
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fclose(fp);

    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            png_bytep pixel = &rows[row][col * channels];
            image.at(row, col) = Color::rgba(pixel[0], pixel[1], pixel[2], channels >= 4 ? pixel[3] : 255);
        }
    }
    return true;
}

static bool write_png(const string& filename, const Image& image) {
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) return false;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        fclose(fp);
        return false;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(
        png_ptr,
        info_ptr,
        image.w,
        image.h,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE,
        PNG_FILTER_TYPE_BASE
    );
    png_write_info(png_ptr, info_ptr);

    vector<vector<unsigned char>> rows(image.h, vector<unsigned char>(image.w * 4));
    vector<png_bytep> row_ptrs(image.h, nullptr);
    for (int row = 0; row < image.h; ++row) {
        row_ptrs[row] = rows[row].data();
        for (int col = 0; col < image.w; ++col) {
            const Color color = image.get(row, col);
            row_ptrs[row][col * 4 + 0] = color.r;
            row_ptrs[row][col * 4 + 1] = color.g;
            row_ptrs[row][col * 4 + 2] = color.b;
            row_ptrs[row][col * 4 + 3] = color.a;
        }
    }

    png_write_image(png_ptr, row_ptrs.data());
    png_write_end(png_ptr, nullptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return true;
}

struct ItemTemplate {
    string name;
    string dir;
    vector<Image> frames;
};

struct ItemInstance {
    int template_index = -1;
    int frame_index = 0;
    int row = 0;
    int col = 0;
    bool visible = true;
};

struct Snapshot {
    Image terrain;
    vector<ItemInstance> items;
    string json_path;
    string png_path;
};

enum class Tool {
    Brush,
    Eraser,
    Fill,
    Picker,
    Rect,
    ItemPlace,
    ItemSelect
};

enum class DragMode {
    Idle,
    Paint,
    Pan,
    Rect,
    ItemMove,
    SliderR,
    SliderG,
    SliderB
};

enum class InputTarget {
    Inactive,
    JsonPath,
    PngPath,
    ItemRoot
};

struct HitBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    string id;
    int value = 0;
};

class MapEditor {
public:
    explicit MapEditor(const string& item_root_arg) {
        init_palette();
        init_window();
        resolve_item_root(item_root_arg);
        load_templates_from_root(item_root);
        terrain.init(128, 96, default_fill_color());
        if (path_exists(json_path)) {
            if (!load_from_json(json_path)) {
                set_status("[Error] Failed to load temp.json, using blank map.");
            }
        } else if (path_exists("Dungeon_Tileset_at.png")) {
            png_path = "Dungeon_Tileset_at.png";
            if (read_png(png_path, terrain)) {
                set_status("Loaded Dungeon_Tileset_at.png as base map.");
            }
        }
        clamp_camera();
        mark_all_dirty();
    }

    ~MapEditor() {
        save_autosave();
        destroy_canvas();
        if (font) XFreeFont(display, font);
        if (display) XCloseDisplay(display);
    }

    void run() {
        while (running) {
            bool had_event = false;
            if (!dirty && !XPending(display)) {
                XEvent event;
                XNextEvent(display, &event);
                had_event = true;
                handle_event(event);
            }

            while (XPending(display)) {
                XEvent event;
                XNextEvent(display, &event);
                had_event = true;
                handle_event(event);
            }

            if (dirty) {
                render();
            }

            if (!had_event) usleep(2000);
        }
    }

private:
    Display* display = nullptr;
    int screen = 0;
    Window window = 0;
    GC gc = 0;
    XFontStruct* font = nullptr;
    XImage* canvas_image = nullptr;

    int window_w = 1560;
    int window_h = 980;
    int panel_w = 430;
    int canvas_w = 1130;
    int canvas_h = 980;

    Image terrain;
    vector<ItemTemplate> templates;
    vector<ItemInstance> items;

    string json_path = "temp.json";
    string png_path = "temp_map.png";
    string item_root = "./items";

    vector<Color> palette;
    int palette_index = 0;
    Color current_color = Color::rgba(77, 100, 141, 255);

    Tool tool = Tool::Brush;
    DragMode drag_mode = DragMode::Idle;
    InputTarget input_target = InputTarget::Inactive;

    bool running = true;
    bool dirty = true;
    bool canvas_dirty = true;
    bool panel_dirty = true;
    bool show_grid = true;
    bool input_active = false;

    int zoom = 8;
    int camera_row = 0;
    int camera_col = 0;
    int brush_radius = 1;
    int mouse_x = 0;
    int mouse_y = 0;
    int hover_row = -1;
    int hover_col = -1;
    int active_template = 0;
    int active_frame = 0;
    int selected_item = -1;

    int pan_anchor_x = 0;
    int pan_anchor_y = 0;
    int pan_start_row = 0;
    int pan_start_col = 0;
    int move_offset_row = 0;
    int move_offset_col = 0;
    int rect_start_row = -1;
    int rect_start_col = -1;
    int rect_end_row = -1;
    int rect_end_col = -1;

    string input_text;
    string status_text = "Ready";
    vector<HitBox> hits;
    vector<Snapshot> undo_stack;
    vector<Snapshot> redo_stack;
    static constexpr int HISTORY_LIMIT = 80;

private:
    Color default_fill_color() const {
        return Color::rgba(30, 43, 58, 255);
    }

    void init_palette() {
        palette = {
            Color::rgba(30, 43, 58),
            Color::rgba(46, 74, 125),
            Color::rgba(77, 100, 141),
            Color::rgba(120, 144, 156),
            Color::rgba(163, 177, 138),
            Color::rgba(212, 163, 115),
            Color::rgba(231, 111, 81),
            Color::rgba(230, 57, 70),
            Color::rgba(123, 44, 191),
            Color::rgba(244, 211, 94),
            Color::rgba(241, 250, 238),
            Color::rgba(17, 17, 17)
        };
        current_color = palette[2];
    }

    void init_window() {
        display = XOpenDisplay(nullptr);
        if (!display) {
            fprintf(stderr, "Cannot open X11 display.\n");
            exit(1);
        }
        screen = DefaultScreen(display);
        window = XCreateSimpleWindow(
            display,
            RootWindow(display, screen),
            20,
            20,
            window_w,
            window_h,
            1,
            BlackPixel(display, screen),
            WhitePixel(display, screen)
        );
        XStoreName(display, window, "Map Editor - Engine Compatible");
        XSelectInput(
            display,
            window,
            ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                PointerMotionMask | StructureNotifyMask
        );
        XMapWindow(display, window);
        gc = DefaultGC(display, screen);
        font = XLoadQueryFont(display, "9x15");
        if (!font) font = XLoadQueryFont(display, "fixed");
        if (font) XSetFont(display, gc, font->fid);
        rebuild_canvas();
    }

    void destroy_canvas() {
        if (canvas_image) {
            XDestroyImage(canvas_image);
            canvas_image = nullptr;
        }
    }

    void rebuild_canvas() {
        destroy_canvas();
        canvas_w = max(480, window_w - panel_w);
        canvas_h = max(360, window_h);
        char* data = static_cast<char*>(malloc(canvas_w * canvas_h * 4));
        if (!data) {
            fprintf(stderr, "Failed to allocate XImage buffer.\n");
            exit(1);
        }
        canvas_image = XCreateImage(
            display,
            DefaultVisual(display, screen),
            DefaultDepth(display, screen),
            ZPixmap,
            0,
            data,
            canvas_w,
            canvas_h,
            32,
            0
        );
        if (!canvas_image) {
            free(data);
            fprintf(stderr, "Failed to create XImage.\n");
            exit(1);
        }
    }

    void resolve_item_root(const string& item_root_arg) {
        if (!item_root_arg.empty()) {
            item_root = item_root_arg;
            return;
        }
        const char* env = getenv("MAP_EDITOR_ITEMS");
        if (env && *env) {
            item_root = env;
            return;
        }
        item_root = "./items";
    }

    void set_status(const string& message) {
        status_text = message;
        mark_panel_dirty();
    }

    void mark_canvas_dirty() {
        canvas_dirty = true;
        dirty = true;
    }

    void mark_panel_dirty() {
        panel_dirty = true;
        dirty = true;
    }

    void mark_all_dirty() {
        mark_canvas_dirty();
        mark_panel_dirty();
    }

    void clamp_camera() {
        int visible_rows = max(1, canvas_h / max(1, zoom));
        int visible_cols = max(1, canvas_w / max(1, zoom));
        camera_row = clamp_int(camera_row, 0, max(0, terrain.h - visible_rows));
        camera_col = clamp_int(camera_col, 0, max(0, terrain.w - visible_cols));
    }

    void sanitize_template_selection() {
        if (templates.empty()) {
            active_template = 0;
            active_frame = 0;
            return;
        }
        active_template = clamp_int(active_template, 0, static_cast<int>(templates.size()) - 1);
        int frame_count = static_cast<int>(templates[active_template].frames.size());
        active_frame = frame_count == 0 ? 0 : clamp_int(active_frame, 0, frame_count - 1);
    }

    void sanitize_selected_item() {
        if (selected_item < 0 || selected_item >= static_cast<int>(items.size())) {
            selected_item = -1;
        }
    }

    void push_undo() {
        undo_stack.push_back({terrain, items, json_path, png_path});
        if (static_cast<int>(undo_stack.size()) > HISTORY_LIMIT) undo_stack.erase(undo_stack.begin());
        redo_stack.clear();
    }

    void restore_snapshot(const Snapshot& snapshot) {
        terrain = snapshot.terrain;
        items = snapshot.items;
        json_path = snapshot.json_path;
        png_path = snapshot.png_path;
        sanitize_selected_item();
        clamp_camera();
        mark_all_dirty();
    }

    void undo() {
        if (undo_stack.empty()) {
            set_status("Nothing to undo.");
            return;
        }
        redo_stack.push_back({terrain, items, json_path, png_path});
        restore_snapshot(undo_stack.back());
        undo_stack.pop_back();
        set_status("Undo complete.");
    }

    void redo() {
        if (redo_stack.empty()) {
            set_status("Nothing to redo.");
            return;
        }
        undo_stack.push_back({terrain, items, json_path, png_path});
        restore_snapshot(redo_stack.back());
        redo_stack.pop_back();
        set_status("Redo complete.");
    }

    vector<Image> load_frames_from_dir(const string& dir, int count_hint = 0) const {
        vector<Image> frames;
        int limit = count_hint > 0 ? count_hint : 256;
        for (int index = 1; index <= limit; ++index) {
            string filename = join_path(dir, to_string(index) + ".png");
            if (!path_exists(filename)) {
                if (count_hint > 0) return {};
                break;
            }
            Image image;
            if (!read_png(filename, image)) {
                if (count_hint > 0) return {};
                break;
            }
            frames.push_back(image);
        }
        return frames;
    }

    string unique_template_name(const string& wanted, const string& dir) const {
        string candidate = wanted.empty() ? file_stem_name(dir) : wanted;
        string base = candidate;
        int suffix = 2;
        auto conflict = [&](const string& name) {
            for (const auto& tp : templates) {
                if (tp.name == name && tp.dir != dir) return true;
            }
            return false;
        };
        while (conflict(candidate)) {
            candidate = base + "_" + to_string(suffix++);
        }
        return candidate;
    }

    int ensure_template_loaded(const string& wanted_name, const string& dir, int count_hint = 0) {
        for (int index = 0; index < static_cast<int>(templates.size()); ++index) {
            if (templates[index].dir == dir) return index;
        }
        vector<Image> frames = load_frames_from_dir(dir, count_hint);
        if (frames.empty()) return -1;
        ItemTemplate item_template;
        item_template.name = unique_template_name(wanted_name, dir);
        item_template.dir = dir;
        item_template.frames = move(frames);
        templates.push_back(move(item_template));
        sanitize_template_selection();
        return static_cast<int>(templates.size()) - 1;
    }

    void load_templates_from_root(const string& root) {
        templates.clear();
        if (!is_dir(root)) {
            set_status("[Error] Item root not found: " + root);
            return;
        }

        DIR* dir = opendir(root.c_str());
        if (!dir) {
            set_status("[Error] Cannot open item root: " + root);
            return;
        }

        vector<pair<string, string>> candidates;
        dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            string name = entry->d_name;
            if (name == "." || name == "..") continue;
            string full = join_path(root, name);
            if (!is_dir(full)) continue;
            if (!path_exists(join_path(full, "1.png"))) continue;
            candidates.push_back({name, full});
        }
        closedir(dir);

        sort(candidates.begin(), candidates.end());
        for (const auto& candidate : candidates) {
            vector<Image> frames = load_frames_from_dir(candidate.second);
            if (frames.empty()) continue;
            ItemTemplate item_template;
            item_template.name = unique_template_name(candidate.first, candidate.second);
            item_template.dir = candidate.second;
            item_template.frames = move(frames);
            templates.push_back(move(item_template));
        }

        sanitize_template_selection();
        set_status("Loaded " + to_string(templates.size()) + " item templates from " + root);
        mark_panel_dirty();
    }

    const ItemTemplate* current_template_ptr() const {
        if (active_template < 0 || active_template >= static_cast<int>(templates.size())) return nullptr;
        return &templates[active_template];
    }

    const Image* current_frame_ptr() const {
        const ItemTemplate* item_template = current_template_ptr();
        if (!item_template) return nullptr;
        if (active_frame < 0 || active_frame >= static_cast<int>(item_template->frames.size())) return nullptr;
        return &item_template->frames[active_frame];
    }

    bool screen_to_pixel(int sx, int sy, int& row, int& col) const {
        if (sx < 0 || sx >= canvas_w || sy < 0 || sy >= canvas_h) return false;
        row = camera_row + sy / max(1, zoom);
        col = camera_col + sx / max(1, zoom);
        return terrain.in_bounds(row, col);
    }

    void begin_input(InputTarget target, const string& current_value) {
        input_target = target;
        input_text = current_value;
        input_active = true;
        mark_panel_dirty();
    }

    void cancel_input() {
        input_target = InputTarget::Inactive;
        input_text.clear();
        input_active = false;
        mark_panel_dirty();
    }

    void commit_input() {
        string value = trim_copy(input_text);
        if (input_target == InputTarget::JsonPath) {
            if (!value.empty()) json_path = value;
        } else if (input_target == InputTarget::PngPath) {
            if (!value.empty()) png_path = value;
        } else if (input_target == InputTarget::ItemRoot) {
            if (!value.empty()) {
                if (!is_dir(value)) {
                    set_status("[Error] Invalid item root: " + value);
                    return;
                }
                item_root = value;
                load_templates_from_root(item_root);
            }
        }
        input_target = InputTarget::Inactive;
        input_text.clear();
        input_active = false;
        mark_panel_dirty();
    }

    void handle_text_input(XKeyEvent* event) {
        KeySym key = XLookupKeysym(event, 0);
        if (key == XK_Return || key == XK_KP_Enter) {
            commit_input();
            return;
        }
        if (key == XK_Escape) {
            cancel_input();
            return;
        }
        if (key == XK_BackSpace) {
            if (!input_text.empty()) input_text.pop_back();
            mark_panel_dirty();
            return;
        }
        if (key == XK_Delete || key == XK_Left || key == XK_Right || key == XK_Home || key == XK_End) {
            return;
        }
        char buffer[32] = {};
        KeySym text_key = 0;
        int len = XLookupString(event, buffer, sizeof(buffer) - 1, &text_key, nullptr);
        if (len <= 0) return;
        for (int index = 0; index < len; ++index) {
            unsigned char ch = static_cast<unsigned char>(buffer[index]);
            if (ch >= 32 && ch < 127) input_text.push_back(static_cast<char>(ch));
        }
        mark_panel_dirty();
    }

    bool item_contains_pixel(const ItemInstance& item, int row, int col) const {
        if (!item.visible) return false;
        if (item.template_index < 0 || item.template_index >= static_cast<int>(templates.size())) return false;
        const ItemTemplate& item_template = templates[item.template_index];
        if (item.frame_index < 0 || item.frame_index >= static_cast<int>(item_template.frames.size())) return false;
        const Image& frame = item_template.frames[item.frame_index];
        int local_row = row - item.row;
        int local_col = col - item.col;
        if (!frame.in_bounds(local_row, local_col)) return false;
        return frame.get(local_row, local_col).a > 0;
    }

    int find_item_at(int row, int col) const {
        for (int index = static_cast<int>(items.size()) - 1; index >= 0; --index) {
            if (item_contains_pixel(items[index], row, col)) return index;
        }
        return -1;
    }

    void paint_at(int row, int col, bool erase) {
        int radius = max(1, brush_radius) - 1;
        Color brush = erase ? Color::rgba(0, 0, 0, 0) : current_color;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy > radius * radius) continue;
                int target_row = row + dy;
                int target_col = col + dx;
                if (!terrain.in_bounds(target_row, target_col)) continue;
                terrain.set(target_row, target_col, brush);
            }
        }
        mark_canvas_dirty();
    }

    void flood_fill(int row, int col, const Color& replacement) {
        if (!terrain.in_bounds(row, col)) return;
        Color source = terrain.get(row, col);
        if (source == replacement) return;
        queue<pair<int, int>> q;
        vector<char> visited(terrain.w * terrain.h, 0);
        q.push({row, col});
        visited[row * terrain.w + col] = 1;
        while (!q.empty()) {
            auto [current_row, current_col] = q.front();
            q.pop();
            terrain.set(current_row, current_col, replacement);
            const int dr[4] = {1, -1, 0, 0};
            const int dc[4] = {0, 0, 1, -1};
            for (int index = 0; index < 4; ++index) {
                int next_row = current_row + dr[index];
                int next_col = current_col + dc[index];
                if (!terrain.in_bounds(next_row, next_col)) continue;
                int key = next_row * terrain.w + next_col;
                if (visited[key]) continue;
                if (terrain.get(next_row, next_col) != source) continue;
                visited[key] = 1;
                q.push({next_row, next_col});
            }
        }
        mark_canvas_dirty();
    }

    void apply_rect_fill() {
        if (rect_start_row < 0 || rect_start_col < 0 || rect_end_row < 0 || rect_end_col < 0) return;
        int row0 = min(rect_start_row, rect_end_row);
        int row1 = max(rect_start_row, rect_end_row);
        int col0 = min(rect_start_col, rect_end_col);
        int col1 = max(rect_start_col, rect_end_col);
        for (int row = row0; row <= row1; ++row) {
            for (int col = col0; col <= col1; ++col) {
                if (!terrain.in_bounds(row, col)) continue;
                terrain.set(row, col, current_color);
            }
        }
        mark_canvas_dirty();
    }

    void place_item_at(int row, int col) {
        if (!current_template_ptr() || !current_frame_ptr()) {
            set_status("No item template loaded.");
            return;
        }
        ItemInstance item;
        item.template_index = active_template;
        item.frame_index = active_frame;
        item.row = clamp_int(row, 0, terrain.h - 1);
        item.col = clamp_int(col, 0, terrain.w - 1);
        item.visible = true;
        items.push_back(item);
        selected_item = static_cast<int>(items.size()) - 1;
        mark_all_dirty();
    }

    void duplicate_selected_item() {
        if (selected_item < 0 || selected_item >= static_cast<int>(items.size())) return;
        ItemInstance copy = items[selected_item];
        copy.row = clamp_int(copy.row + 1, 0, terrain.h - 1);
        copy.col = clamp_int(copy.col + 1, 0, terrain.w - 1);
        items.push_back(copy);
        selected_item = static_cast<int>(items.size()) - 1;
        mark_all_dirty();
    }

    void delete_selected_item() {
        if (selected_item < 0 || selected_item >= static_cast<int>(items.size())) return;
        items.erase(items.begin() + selected_item);
        selected_item = -1;
        mark_all_dirty();
    }

    void apply_current_template_to_selected() {
        if (selected_item < 0 || selected_item >= static_cast<int>(items.size())) return;
        if (!current_template_ptr() || !current_frame_ptr()) return;
        items[selected_item].template_index = active_template;
        items[selected_item].frame_index = active_frame;
        items[selected_item].visible = true;
        mark_all_dirty();
    }

    void resize_map_keep(int new_w, int new_h) {
        push_undo();
        terrain.resize_keep(new_w, new_h, default_fill_color());
        vector<ItemInstance> kept;
        for (const auto& item : items) {
            if (item.row >= 0 && item.row < terrain.h && item.col >= 0 && item.col < terrain.w) kept.push_back(item);
        }
        items.swap(kept);
        sanitize_selected_item();
        clamp_camera();
        mark_all_dirty();
    }

    void clear_map() {
        push_undo();
        terrain.clear(default_fill_color());
        items.clear();
        selected_item = -1;
        mark_all_dirty();
    }

    void new_blank_map() {
        push_undo();
        terrain.init(128, 96, default_fill_color());
        items.clear();
        camera_row = 0;
        camera_col = 0;
        selected_item = -1;
        png_path = "temp_map.png";
        json_path = "temp.json";
        mark_all_dirty();
    }

    bool save_to_json(const string& json_file, const string& png_file, bool update_current_paths = true) {
        if (!write_png(png_file, terrain)) {
            set_status("[Error] Failed to write PNG: " + png_file);
            return false;
        }

        vector<string> item_names;
        map<string, json> item_blocks;
        for (const auto& item : items) {
            if (item.template_index < 0 || item.template_index >= static_cast<int>(templates.size())) continue;
            const ItemTemplate& item_template = templates[item.template_index];
            if (item.frame_index < 0 || item.frame_index >= static_cast<int>(item_template.frames.size())) continue;
            if (!item_blocks.count(item_template.name)) {
                item_names.push_back(item_template.name);
                json block;
                block["Path"] = item_template.dir;
                block["access_able"] = 1;
                block["tot"] = static_cast<int>(item_template.frames.size());
                block["sum"] = 0;
                block["positions"] = json::array();
                block["visible"] = json::array({true});
                block["chosen"] = json::array({0});
                item_blocks[item_template.name] = block;
            }
            json& block = item_blocks[item_template.name];
            block["sum"] = block["sum"].get<int>() + 1;
            block["positions"].push_back({item.row, item.col});
            block["visible"].push_back(item.visible);
            block["chosen"].push_back(item.visible ? item.frame_index + 1 : 0);
        }

        json document;
        document["Path"] = png_file;
        document["Item_names"] = item_names;
        document["Items"] = json::object();
        for (const auto& name : item_names) {
            document["Items"][name] = item_blocks[name];
        }
        document["Editor"] = {
            {"zoom", zoom},
            {"camera_row", camera_row},
            {"camera_col", camera_col},
            {"show_grid", show_grid},
            {"brush_radius", brush_radius},
            {"item_root", item_root},
            {"current_template", active_template},
            {"current_frame", active_frame},
            {"selected_item", selected_item},
            {"current_color", current_color.to_rgb_hex()}
        };

        ofstream out(json_file);
        if (!out.is_open()) {
            set_status("[Error] Failed to write JSON: " + json_file);
            return false;
        }
        out << document.dump(4);
        out.close();

        if (update_current_paths) {
            json_path = json_file;
            png_path = png_file;
        }
        set_status("Saved " + json_file + " and " + png_file);
        return true;
    }

    bool save_current_files() {
        if (json_path.empty()) json_path = "temp.json";
        if (png_path.empty()) png_path = "temp_map.png";
        return save_to_json(json_path, png_path, true);
    }

    bool save_timestamp_copy() {
        string stamp = "map_export_" + now_string();
        string export_json = stamp + ".json";
        string export_png = stamp + ".png";
        if (!save_to_json(export_json, export_png, false)) return false;
        set_status("Exported timestamp copy: " + export_json);
        return true;
    }

    bool save_autosave() {
        if (terrain.w <= 0 || terrain.h <= 0) return false;
        string auto_json = "map_editor_autosave.json";
        string auto_png = "map_editor_autosave.png";
        return save_to_json(auto_json, auto_png, false);
    }

    bool load_from_json(const string& file) {
        ifstream in(file);
        if (!in.is_open()) {
            set_status("[Error] Cannot open JSON: " + file);
            return false;
        }

        json document;
        try {
            in >> document;
        } catch (...) {
            set_status("[Error] Invalid JSON: " + file);
            return false;
        }

        if (!document.contains("Path")) {
            set_status("[Error] Missing Path in JSON: " + file);
            return false;
        }

        string image_path = document["Path"].get<string>();
        Image loaded;
        if (!read_png(image_path, loaded)) {
            string relative = join_path(dirname_of(file), image_path);
            if (!read_png(relative, loaded)) {
                set_status("[Error] Failed to load map PNG: " + image_path);
                return false;
            }
            image_path = relative;
        }

        json editor_state;
        if (document.contains("Editor")) {
            editor_state = document["Editor"];
            string loaded_root = editor_state.value("item_root", item_root);
            if (is_dir(loaded_root) && loaded_root != item_root) {
                item_root = loaded_root;
                load_templates_from_root(item_root);
            }
        }

        vector<ItemInstance> loaded_items;
        if (document.contains("Items") && document.contains("Item_names")) {
            auto names = document["Item_names"].get<vector<string>>();
            json item_object = document["Items"];
            for (const string& name : names) {
                if (!item_object.contains(name)) continue;
                json block = item_object[name];
                string dir = block.value("Path", join_path(item_root, name));
                int total = block.value("tot", 0);
                int template_index = ensure_template_loaded(name, dir, total);
                if (template_index < 0) continue;

                vector<pair<int, int>> positions;
                if (block.contains("positions")) {
                    for (const auto& pos : block["positions"]) {
                        if (pos.is_array() && pos.size() >= 2) {
                            positions.push_back({pos[0].get<int>(), pos[1].get<int>()});
                        }
                    }
                }

                vector<int> chosen;
                if (block.contains("chosen")) {
                    chosen = block["chosen"].get<vector<int>>();
                }

                vector<bool> visible;
                if (block.contains("visible")) {
                    visible = block["visible"].get<vector<bool>>();
                }

                for (int index = 0; index < static_cast<int>(positions.size()); ++index) {
                    ItemInstance item;
                    item.template_index = template_index;
                    item.row = positions[index].first;
                    item.col = positions[index].second;
                    int chosen_value = index + 1 < static_cast<int>(chosen.size()) ? chosen[index + 1] : 1;
                    bool is_visible = index + 1 < static_cast<int>(visible.size()) ? visible[index + 1] : true;
                    item.visible = is_visible && chosen_value > 0;
                    item.frame_index = clamp_int(chosen_value - 1, 0, max(0, static_cast<int>(templates[template_index].frames.size()) - 1));
                    loaded_items.push_back(item);
                }
            }
        }

        terrain = loaded;
        items = move(loaded_items);
        json_path = file;
        png_path = image_path;
        selected_item = -1;

        if (!editor_state.is_null()) {
            zoom = clamp_int(editor_state.value("zoom", zoom), 2, 48);
            camera_row = editor_state.value("camera_row", camera_row);
            camera_col = editor_state.value("camera_col", camera_col);
            show_grid = editor_state.value("show_grid", show_grid);
            brush_radius = clamp_int(editor_state.value("brush_radius", brush_radius), 1, 24);
            active_template = editor_state.value("current_template", active_template);
            active_frame = editor_state.value("current_frame", active_frame);
            int rgb = editor_state.value("current_color", current_color.to_rgb_hex());
            current_color = Color::rgba((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
        }

        sanitize_template_selection();
        clamp_camera();
        mark_all_dirty();
        set_status("Loaded project: " + file);
        return true;
    }

    void cycle_template(int delta) {
        if (templates.empty()) return;
        active_template = (active_template + delta) % static_cast<int>(templates.size());
        if (active_template < 0) active_template += static_cast<int>(templates.size());
        active_frame = 0;
        mark_panel_dirty();
    }

    void cycle_frame(int delta) {
        const ItemTemplate* item_template = current_template_ptr();
        if (!item_template || item_template->frames.empty()) return;
        int frame_count = static_cast<int>(item_template->frames.size());
        active_frame = (active_frame + delta) % frame_count;
        if (active_frame < 0) active_frame += frame_count;
        mark_panel_dirty();
    }

    void adjust_slider(const string& id, int mouse_pos_x) {
        for (const auto& hit : hits) {
            if (hit.id != id) continue;
            int value = clamp_int((mouse_pos_x - hit.x) * 255 / max(1, hit.w), 0, 255);
            if (id == "slider_r") current_color.r = static_cast<unsigned char>(value);
            if (id == "slider_g") current_color.g = static_cast<unsigned char>(value);
            if (id == "slider_b") current_color.b = static_cast<unsigned char>(value);
            mark_panel_dirty();
            return;
        }
    }

    void canvas_put_pixel(int x, int y, unsigned long value) {
        if (!canvas_image) return;
        if (x < 0 || x >= canvas_w || y < 0 || y >= canvas_h) return;
        auto* row = reinterpret_cast<uint32_t*>(canvas_image->data + y * canvas_image->bytes_per_line);
        row[x] = static_cast<uint32_t>(value);
    }

    unsigned long canvas_get_pixel(int x, int y) const {
        if (!canvas_image) return 0;
        if (x < 0 || x >= canvas_w || y < 0 || y >= canvas_h) return 0;
        auto* row = reinterpret_cast<uint32_t*>(canvas_image->data + y * canvas_image->bytes_per_line);
        return row[x];
    }

    void canvas_fill_rect(int x, int y, int w, int h, unsigned long value) {
        int x0 = max(0, x);
        int y0 = max(0, y);
        int x1 = min(canvas_w, x + w);
        int y1 = min(canvas_h, y + h);
        uint32_t packed = static_cast<uint32_t>(value);
        for (int py = y0; py < y1; ++py) {
            auto* row = reinterpret_cast<uint32_t*>(canvas_image->data + py * canvas_image->bytes_per_line);
            for (int px = x0; px < x1; ++px) row[px] = packed;
        }
    }

    void canvas_frame_rect(int x, int y, int w, int h, unsigned long value) {
        if (w <= 0 || h <= 0) return;
        canvas_fill_rect(x, y, w, 1, value);
        canvas_fill_rect(x, y + h - 1, w, 1, value);
        canvas_fill_rect(x, y, 1, h, value);
        canvas_fill_rect(x + w - 1, y, 1, h, value);
    }

    Color checker_color(int row, int col) const {
        bool even = ((row + col) & 1) == 0;
        return even ? Color::rgba(52, 61, 72) : Color::rgba(38, 46, 56);
    }

    void draw_scaled_pixel(int screen_x, int screen_y, const Color& color, int row, int col) {
        Color base = color.a == 0 ? checker_color(row, col) : color;
        canvas_fill_rect(screen_x, screen_y, zoom, zoom, pack_rgb(base));
    }

    void render_canvas() {
        for (int y = 0; y < canvas_h; ++y) {
            Color line = Color::rgba(18 + y * 8 / max(1, canvas_h), 24 + y * 12 / max(1, canvas_h), 32 + y * 16 / max(1, canvas_h));
            canvas_fill_rect(0, y, canvas_w, 1, pack_rgb(line));
        }

        int visible_rows = canvas_h / max(1, zoom) + 2;
        int visible_cols = canvas_w / max(1, zoom) + 2;
        int row_start = camera_row;
        int row_end = min(terrain.h, camera_row + visible_rows);
        int col_start = camera_col;
        int col_end = min(terrain.w, camera_col + visible_cols);

        for (int row = row_start; row < row_end; ++row) {
            for (int col = col_start; col < col_end; ++col) {
                int sx = (col - camera_col) * zoom;
                int sy = (row - camera_row) * zoom;
                draw_scaled_pixel(sx, sy, terrain.get(row, col), row, col);
            }
        }

        for (int item_index = 0; item_index < static_cast<int>(items.size()); ++item_index) {
            const ItemInstance& item = items[item_index];
            if (!item.visible) continue;
            if (item.template_index < 0 || item.template_index >= static_cast<int>(templates.size())) continue;
            const ItemTemplate& item_template = templates[item.template_index];
            if (item.frame_index < 0 || item.frame_index >= static_cast<int>(item_template.frames.size())) continue;
            const Image& frame = item_template.frames[item.frame_index];

            int src_row_begin = max(0, camera_row - item.row);
            int src_col_begin = max(0, camera_col - item.col);
            int src_row_end = min(frame.h, camera_row + visible_rows - item.row);
            int src_col_end = min(frame.w, camera_col + visible_cols - item.col);

            for (int src_row = src_row_begin; src_row < src_row_end; ++src_row) {
                for (int src_col = src_col_begin; src_col < src_col_end; ++src_col) {
                    Color sprite = frame.get(src_row, src_col);
                    if (sprite.a == 0) continue;
                    int map_row = item.row + src_row;
                    int map_col = item.col + src_col;
                    int sx = (map_col - camera_col) * zoom;
                    int sy = (map_row - camera_row) * zoom;
                    unsigned long under = canvas_get_pixel(sx, sy);
                    Color base = Color::rgba((under >> 16) & 0xFF, (under >> 8) & 0xFF, under & 0xFF, 255);
                    Color blended = blend_over(base, sprite);
                    canvas_fill_rect(sx, sy, zoom, zoom, pack_rgb(blended));
                }
            }

            if (item_index == selected_item) {
                int sx = (item.col - camera_col) * zoom;
                int sy = (item.row - camera_row) * zoom;
                canvas_frame_rect(sx, sy, max(1, frame.w * zoom), max(1, frame.h * zoom), 0xFFFFFF);
            }
        }

        if (show_grid && zoom >= 8) {
            unsigned long grid = 0x2A3442;
            for (int row = row_start; row <= row_end; ++row) {
                int sy = (row - camera_row) * zoom;
                canvas_fill_rect(0, sy, canvas_w, 1, grid);
            }
            for (int col = col_start; col <= col_end; ++col) {
                int sx = (col - camera_col) * zoom;
                canvas_fill_rect(sx, 0, 1, canvas_h, grid);
            }
        }

        if (drag_mode == DragMode::Rect && rect_start_row >= 0 && rect_end_row >= 0) {
            int row0 = min(rect_start_row, rect_end_row);
            int row1 = max(rect_start_row, rect_end_row);
            int col0 = min(rect_start_col, rect_end_col);
            int col1 = max(rect_start_col, rect_end_col);
            int sx = (col0 - camera_col) * zoom;
            int sy = (row0 - camera_row) * zoom;
            int sw = (col1 - col0 + 1) * zoom;
            int sh = (row1 - row0 + 1) * zoom;
            canvas_frame_rect(sx, sy, sw, sh, 0xFFFFFF);
        }
    }

    void draw_text(int x, int y, const string& text, unsigned long color) {
        XSetForeground(display, gc, color);
        XDrawString(display, window, gc, x, y, text.c_str(), static_cast<int>(text.size()));
    }

    int text_width(const string& text) const {
        if (!font) return static_cast<int>(text.size()) * 8;
        return XTextWidth(font, text.c_str(), static_cast<int>(text.size()));
    }

    string ellipsize(const string& text, int max_width) const {
        if (text_width(text) <= max_width) return text;
        string output = text;
        while (!output.empty() && text_width(output + "...") > max_width) output.pop_back();
        return output + "...";
    }

    void add_hit(int x, int y, int w, int h, const string& id, int value = 0) {
        hits.push_back({x, y, w, h, id, value});
    }

    void draw_button(int x, int y, int w, int h, const string& label, const string& id, bool active = false, int value = 0) {
        XSetForeground(display, gc, active ? 0x245B9C : 0x162434);
        XFillRectangle(display, window, gc, x, y, w, h);
        XSetForeground(display, gc, active ? 0x9DC7FF : 0x35506E);
        XDrawRectangle(display, window, gc, x, y, w, h);
        draw_text(x + 8, y + h / 2 + 5, ellipsize(label, w - 16), 0xE6F1FF);
        add_hit(x, y, w, h, id, value);
    }

    void draw_field(int x, int y, int w, int h, const string& label, const string& value, InputTarget target) {
        draw_text(x, y - 4, label, 0xAFCBEA);
        bool active = input_active && input_target == target;
        XSetForeground(display, gc, 0x102030);
        XFillRectangle(display, window, gc, x, y, w, h);
        XSetForeground(display, gc, active ? 0x6EA8FF : 0x35506E);
        XDrawRectangle(display, window, gc, x, y, w, h);
        string shown = active ? input_text + "_" : value;
        draw_text(x + 6, y + h / 2 + 5, ellipsize(shown, w - 12), 0xE6F1FF);
        string hit_id = target == InputTarget::JsonPath ? "field_json" : target == InputTarget::PngPath ? "field_png" : "field_items";
        add_hit(x, y, w, h, hit_id, 0);
    }

    void draw_swatch(int x, int y, int w, int h, const Color& color, int value, bool active) {
        XSetForeground(display, gc, pack_rgb(color));
        XFillRectangle(display, window, gc, x, y, w, h);
        XSetForeground(display, gc, active ? 0xFFFFFF : 0x22354D);
        XDrawRectangle(display, window, gc, x, y, w, h);
        add_hit(x, y, w, h, "palette", value);
    }

    void draw_slider(int x, int y, int w, int h, int value, const string& id, unsigned long fill) {
        XSetForeground(display, gc, 0x142435);
        XFillRectangle(display, window, gc, x, y, w, h);
        XSetForeground(display, gc, fill);
        int fill_w = clamp_int(value * w / 255, 0, w);
        XFillRectangle(display, window, gc, x, y, fill_w, h);
        XSetForeground(display, gc, 0xE6F1FF);
        int knob_x = x + fill_w - 2;
        XFillRectangle(display, window, gc, knob_x, y - 2, 4, h + 4);
        add_hit(x, y, w, h, id, 0);
    }

    void draw_preview_image(int x, int y, int w, int h, const Image* image) {
        XSetForeground(display, gc, 0x102030);
        XFillRectangle(display, window, gc, x, y, w, h);
        XSetForeground(display, gc, 0x35506E);
        XDrawRectangle(display, window, gc, x, y, w, h);
        if (!image || image->w <= 0 || image->h <= 0) return;

        for (int py = 0; py < h; ++py) {
            int src_row = py * image->h / max(1, h);
            for (int px = 0; px < w; ++px) {
                int src_col = px * image->w / max(1, w);
                Color color = image->get(src_row, src_col);
                Color shown = color.a == 0 ? checker_color(src_row, src_col) : color;
                XSetForeground(display, gc, pack_rgb(shown));
                XDrawPoint(display, window, gc, x + px, y + py);
            }
        }
        XSetForeground(display, gc, 0x6EA8FF);
        XDrawRectangle(display, window, gc, x, y, w, h);
    }

    string tool_name() const {
        switch (tool) {
            case Tool::Brush: return "Brush";
            case Tool::Eraser: return "Eraser";
            case Tool::Fill: return "Fill";
            case Tool::Picker: return "Picker";
            case Tool::Rect: return "Rect";
            case Tool::ItemPlace: return "Item Place";
            case Tool::ItemSelect: return "Item Select";
        }
        return "Brush";
    }

    void draw_panel() {
        hits.clear();
        int px = canvas_w;
        XSetForeground(display, gc, 0x0B1422);
        XFillRectangle(display, window, gc, px, 0, panel_w, window_h);
        XSetForeground(display, gc, 0x22354D);
        XDrawLine(display, window, gc, px, 0, px, window_h);

        int x = px + 18;
        int y = 18;
        XSetForeground(display, gc, 0x12263D);
        XFillRectangle(display, window, gc, x, y, panel_w - 36, 72);
        draw_text(x + 12, y + 24, "GAME MAP EDITOR", 0xECF5FF);
        draw_text(x + 12, y + 44, "Direct PNG editing + engine JSON items", 0xAFCBEA);
        draw_text(x + 12, y + 64, "Ctrl+S save  Ctrl+O load  Ctrl+Z/Y history", 0x8FB5D9);

        y += 96;
        draw_text(x, y, "Tools", 0xECF5FF);
        y += 12;
        draw_button(x, y, 90, 28, "Brush", "tool", tool == Tool::Brush, static_cast<int>(Tool::Brush));
        draw_button(x + 98, y, 90, 28, "Eraser", "tool", tool == Tool::Eraser, static_cast<int>(Tool::Eraser));
        draw_button(x + 196, y, 90, 28, "Fill", "tool", tool == Tool::Fill, static_cast<int>(Tool::Fill));
        draw_button(x + 294, y, 100, 28, "Picker", "tool", tool == Tool::Picker, static_cast<int>(Tool::Picker));
        y += 34;
        draw_button(x, y, 90, 28, "Rect", "tool", tool == Tool::Rect, static_cast<int>(Tool::Rect));
        draw_button(x + 98, y, 140, 28, "Item Place", "tool", tool == Tool::ItemPlace, static_cast<int>(Tool::ItemPlace));
        draw_button(x + 246, y, 148, 28, "Item Select", "tool", tool == Tool::ItemSelect, static_cast<int>(Tool::ItemSelect));

        y += 48;
        draw_text(x, y, "Color", 0xECF5FF);
        y += 10;
        for (int index = 0; index < static_cast<int>(palette.size()); ++index) {
            int sx = x + (index % 6) * 64;
            int sy = y + (index / 6) * 26;
            draw_swatch(sx, sy, 56, 20, palette[index], index, index == palette_index);
        }
        y += 58;
        XSetForeground(display, gc, pack_rgb(current_color));
        XFillRectangle(display, window, gc, x, y, 76, 46);
        XSetForeground(display, gc, 0xD5EAFF);
        XDrawRectangle(display, window, gc, x, y, 76, 46);
        draw_text(x + 88, y + 12, "R", 0xFFB4B4);
        draw_slider(x + 108, y + 2, 286, 12, current_color.r, "slider_r", 0xE05D5D);
        draw_text(x + 88, y + 28, "G", 0xB3FFC7);
        draw_slider(x + 108, y + 18, 286, 12, current_color.g, "slider_g", 0x4DA66F);
        draw_text(x + 88, y + 44, "B", 0xBDD3FF);
        draw_slider(x + 108, y + 34, 286, 12, current_color.b, "slider_b", 0x4A71C9);

        y += 68;
        draw_text(x, y, "Canvas", 0xECF5FF);
        y += 12;
        draw_button(x, y, 88, 26, "Zoom +", "zoom_in");
        draw_button(x + 96, y, 88, 26, "Zoom -", "zoom_out");
        draw_button(x + 192, y, 88, 26, "Brush +", "brush_in");
        draw_button(x + 288, y, 106, 26, "Brush -", "brush_out");
        y += 32;
        draw_button(x, y, 88, 26, show_grid ? "Grid On" : "Grid Off", "toggle_grid", show_grid);
        draw_button(x + 96, y, 88, 26, "Undo", "undo");
        draw_button(x + 192, y, 88, 26, "Redo", "redo");
        draw_button(x + 288, y, 106, 26, "Clear", "clear_map");
        y += 32;
        draw_button(x, y, 88, 26, "W +", "w_in");
        draw_button(x + 96, y, 88, 26, "W -", "w_out");
        draw_button(x + 192, y, 88, 26, "H +", "h_in");
        draw_button(x + 288, y, 106, 26, "H -", "h_out");
        y += 32;
        draw_button(x, y, 120, 26, "New Blank", "new_map");
        draw_text(x + 132, y + 18, "Map: " + to_string(terrain.w) + " x " + to_string(terrain.h), 0xB7D2F0);

        y += 48;
        draw_text(x, y, "Files", 0xECF5FF);
        y += 18;
        draw_field(x, y, 394, 24, "JSON", json_path, InputTarget::JsonPath);
        y += 42;
        draw_field(x, y, 394, 24, "PNG", png_path, InputTarget::PngPath);
        y += 40;
        draw_button(x, y, 88, 26, "Load", "load_json");
        draw_button(x + 96, y, 88, 26, "Save", "save_json");
        draw_button(x + 192, y, 112, 26, "Timestamp", "save_stamp");
        draw_button(x + 312, y, 82, 26, "Open Temp", "load_temp");

        y += 44;
        draw_text(x, y, "Items", 0xECF5FF);
        y += 18;
        draw_field(x, y, 394, 24, "Item Root", item_root, InputTarget::ItemRoot);
        y += 40;
        draw_button(x, y, 88, 26, "Reload", "reload_items");
        draw_button(x + 96, y, 88, 26, "Tpl <", "tpl_prev");
        draw_button(x + 192, y, 88, 26, "Tpl >", "tpl_next");
        draw_button(x + 288, y, 106, 26, "Apply Sel", "apply_selected");
        y += 32;
        draw_button(x, y, 88, 26, "Frame <", "frame_prev");
        draw_button(x + 96, y, 88, 26, "Frame >", "frame_next");
        draw_button(x + 192, y, 88, 26, "Copy", "copy_item");
        draw_button(x + 288, y, 106, 26, selected_item >= 0 && selected_item < static_cast<int>(items.size()) && items[selected_item].visible ? "Hide" : "Show", "toggle_item_visible");
        y += 32;
        draw_button(x, y, 120, 26, "Delete Item", "delete_item");
        draw_button(x + 128, y, 120, 26, "Unselect", "unselect_item");
        draw_text(x + 258, y + 18, "Templates: " + to_string(templates.size()), 0xB7D2F0);

        y += 40;
        draw_text(x, y, "Template Browser", 0xECF5FF);
        y += 10;
        int list_top = max(0, active_template - 3);
        int list_bottom = min(static_cast<int>(templates.size()), list_top + 6);
        for (int index = list_top; index < list_bottom; ++index) {
            const ItemTemplate& item_template = templates[index];
            string label = item_template.name + " (" + to_string(item_template.frames.size()) + ")";
            draw_button(x, y, 214, 22, label, "pick_template", index == active_template, index);
            y += 24;
        }
        if (templates.empty()) {
            draw_text(x, y + 16, "No item templates found.", 0x89AED1);
            y += 28;
        }

        const ItemTemplate* item_template = current_template_ptr();
        string template_name = item_template ? item_template->name : "(none)";
        draw_text(x + 228, y - 12, "Current: " + ellipsize(template_name, 160), 0xB7D2F0);
        draw_preview_image(x + 228, y, 166, 124, current_frame_ptr());
        if (item_template) {
            draw_text(x + 228, y + 142, "Frame " + to_string(active_frame + 1) + " / " + to_string(item_template->frames.size()), 0xB7D2F0);
            draw_text(x + 228, y + 162, ellipsize(item_template->dir, 166), 0x8FB5D9);
        }

        y += 190;
        draw_text(x, y, "Status", 0xECF5FF);
        y += 18;
        draw_text(x, y, ellipsize(status_text, 394), 0xAFCBEA);
        y += 24;
        string hover = hover_row >= 0 ? ("Hover: (" + to_string(hover_row) + ", " + to_string(hover_col) + ")") : "Hover: (out)";
        draw_text(x, y, hover, 0xAFCBEA);
        y += 20;
        string selection = selected_item >= 0 && selected_item < static_cast<int>(items.size())
                               ? ("Selected item #" + to_string(selected_item) + " at (" + to_string(items[selected_item].row) + ", " + to_string(items[selected_item].col) + ")")
                               : "Selected item: none";
        draw_text(x, y, ellipsize(selection, 394), 0xAFCBEA);
        y += 20;
        draw_text(x, y, "Tool=" + tool_name() + "  Zoom=" + to_string(zoom) + "  Brush=" + to_string(brush_radius), 0xAFCBEA);
        draw_text(x, window_h - 18, "LMB edit  RMB pick  MMB pan  Wheel zoom", 0x8FB5D9);
    }

    int hit_index_at(int x, int y) const {
        for (int index = static_cast<int>(hits.size()) - 1; index >= 0; --index) {
            const HitBox& hit = hits[index];
            if (x >= hit.x && x < hit.x + hit.w && y >= hit.y && y < hit.y + hit.h) return index;
        }
        return -1;
    }

    void handle_panel_action(const HitBox& hit) {
        if (input_active && hit.id.rfind("field_", 0) != 0) commit_input();

        if (hit.id == "tool") {
            tool = static_cast<Tool>(hit.value);
            mark_panel_dirty();
            return;
        }
        if (hit.id == "palette") {
            palette_index = hit.value;
            if (palette_index >= 0 && palette_index < static_cast<int>(palette.size())) current_color = palette[palette_index];
            mark_panel_dirty();
            return;
        }
        if (hit.id == "field_json") {
            begin_input(InputTarget::JsonPath, json_path);
            return;
        }
        if (hit.id == "field_png") {
            begin_input(InputTarget::PngPath, png_path);
            return;
        }
        if (hit.id == "field_items") {
            begin_input(InputTarget::ItemRoot, item_root);
            return;
        }
        if (hit.id == "zoom_in") zoom = min(48, zoom + 1);
        else if (hit.id == "zoom_out") zoom = max(2, zoom - 1);
        else if (hit.id == "brush_in") brush_radius = min(24, brush_radius + 1);
        else if (hit.id == "brush_out") brush_radius = max(1, brush_radius - 1);
        else if (hit.id == "toggle_grid") show_grid = !show_grid;
        else if (hit.id == "undo") {
            undo();
            return;
        } else if (hit.id == "redo") {
            redo();
            return;
        } else if (hit.id == "clear_map") {
            clear_map();
            set_status("Map cleared.");
            return;
        } else if (hit.id == "w_in") {
            resize_map_keep(terrain.w + 16, terrain.h);
            set_status("Map width increased.");
            return;
        } else if (hit.id == "w_out") {
            resize_map_keep(max(16, terrain.w - 16), terrain.h);
            set_status("Map width decreased.");
            return;
        } else if (hit.id == "h_in") {
            resize_map_keep(terrain.w, terrain.h + 16);
            set_status("Map height increased.");
            return;
        } else if (hit.id == "h_out") {
            resize_map_keep(terrain.w, max(16, terrain.h - 16));
            set_status("Map height decreased.");
            return;
        } else if (hit.id == "new_map") {
            new_blank_map();
            set_status("New blank map created.");
            return;
        } else if (hit.id == "load_json") {
            load_from_json(json_path);
            return;
        } else if (hit.id == "save_json") {
            save_current_files();
            return;
        } else if (hit.id == "save_stamp") {
            save_timestamp_copy();
            return;
        } else if (hit.id == "load_temp") {
            load_from_json("temp.json");
            return;
        } else if (hit.id == "reload_items") {
            load_templates_from_root(item_root);
            return;
        } else if (hit.id == "tpl_prev") {
            cycle_template(-1);
            return;
        } else if (hit.id == "tpl_next") {
            cycle_template(1);
            return;
        } else if (hit.id == "frame_prev") {
            cycle_frame(-1);
            return;
        } else if (hit.id == "frame_next") {
            cycle_frame(1);
            return;
        } else if (hit.id == "apply_selected") {
            push_undo();
            apply_current_template_to_selected();
            set_status("Applied current template/frame to selected item.");
            return;
        } else if (hit.id == "copy_item") {
            push_undo();
            duplicate_selected_item();
            set_status("Selected item duplicated.");
            return;
        } else if (hit.id == "toggle_item_visible") {
            if (selected_item >= 0 && selected_item < static_cast<int>(items.size())) {
                push_undo();
                items[selected_item].visible = !items[selected_item].visible;
                mark_all_dirty();
                set_status(items[selected_item].visible ? "Selected item visible." : "Selected item hidden.");
            }
            return;
        } else if (hit.id == "delete_item") {
            push_undo();
            delete_selected_item();
            set_status("Selected item deleted.");
            return;
        } else if (hit.id == "unselect_item") {
            selected_item = -1;
            mark_all_dirty();
            return;
        } else if (hit.id == "pick_template") {
            active_template = hit.value;
            active_frame = 0;
            sanitize_template_selection();
            mark_panel_dirty();
            return;
        }

        clamp_camera();
        mark_all_dirty();
    }

    void begin_canvas_action(int sx, int sy) {
        int row = 0;
        int col = 0;
        if (!screen_to_pixel(sx, sy, row, col)) return;

        if (tool == Tool::Brush) {
            push_undo();
            drag_mode = DragMode::Paint;
            paint_at(row, col, false);
            return;
        }
        if (tool == Tool::Eraser) {
            push_undo();
            drag_mode = DragMode::Paint;
            paint_at(row, col, true);
            return;
        }
        if (tool == Tool::Fill) {
            push_undo();
            flood_fill(row, col, current_color);
            return;
        }
        if (tool == Tool::Picker) {
            current_color = terrain.get(row, col);
            current_color.a = 255;
            mark_panel_dirty();
            return;
        }
        if (tool == Tool::Rect) {
            push_undo();
            drag_mode = DragMode::Rect;
            rect_start_row = rect_end_row = row;
            rect_start_col = rect_end_col = col;
            mark_canvas_dirty();
            return;
        }
        if (tool == Tool::ItemPlace) {
            push_undo();
            place_item_at(row, col);
            set_status("Item placed.");
            return;
        }
        if (tool == Tool::ItemSelect) {
            int hit_item = find_item_at(row, col);
            selected_item = hit_item;
            if (hit_item >= 0) {
                push_undo();
                move_offset_row = row - items[hit_item].row;
                move_offset_col = col - items[hit_item].col;
                drag_mode = DragMode::ItemMove;
                set_status("Dragging selected item.");
            } else {
                set_status("No item at cursor.");
            }
            mark_all_dirty();
        }
    }

    void update_hover() {
        int row = -1;
        int col = -1;
        if (screen_to_pixel(mouse_x, mouse_y, row, col)) {
            hover_row = row;
            hover_col = col;
        } else {
            hover_row = -1;
            hover_col = -1;
        }
        mark_panel_dirty();
    }

    void handle_key_press(XKeyEvent* event) {
        if (input_active) {
            handle_text_input(event);
            return;
        }

        KeySym key = XLookupKeysym(event, 0);
        bool ctrl = (event->state & ControlMask) != 0;
        if (ctrl && (key == XK_s || key == XK_S)) {
            save_current_files();
            return;
        }
        if (ctrl && (key == XK_o || key == XK_O)) {
            load_from_json(json_path);
            return;
        }
        if (ctrl && (key == XK_z || key == XK_Z)) {
            undo();
            return;
        }
        if (ctrl && (key == XK_y || key == XK_Y)) {
            redo();
            return;
        }

        if (key == XK_Escape) {
            if (drag_mode == DragMode::Rect) {
                drag_mode = DragMode::Idle;
                rect_start_row = rect_start_col = rect_end_row = rect_end_col = -1;
                mark_canvas_dirty();
                return;
            }
            running = false;
            return;
        }
        if (key == XK_b || key == XK_B) tool = Tool::Brush;
        else if (key == XK_e || key == XK_E) tool = Tool::Eraser;
        else if (key == XK_f || key == XK_F) tool = Tool::Fill;
        else if (key == XK_p || key == XK_P) tool = Tool::Picker;
        else if (key == XK_r || key == XK_R) tool = Tool::Rect;
        else if (key == XK_i || key == XK_I) tool = Tool::ItemPlace;
        else if (key == XK_v || key == XK_V) tool = Tool::ItemSelect;
        else if (key == XK_g || key == XK_G) show_grid = !show_grid;
        else if (key == XK_minus || key == XK_KP_Subtract) zoom = max(2, zoom - 1);
        else if (key == XK_equal || key == XK_plus || key == XK_KP_Add) zoom = min(48, zoom + 1);
        else if (key == XK_bracketleft) cycle_frame(-1);
        else if (key == XK_bracketright) cycle_frame(1);
        else if (key == XK_comma) cycle_template(-1);
        else if (key == XK_period) cycle_template(1);
        else if (key == XK_Delete || key == XK_BackSpace) {
            if (selected_item >= 0) {
                push_undo();
                delete_selected_item();
                set_status("Selected item deleted.");
                return;
            }
        } else if (key == XK_Up || key == XK_w || key == XK_W) camera_row -= 4;
        else if (key == XK_Down || key == XK_s || key == XK_S) camera_row += 4;
        else if (key == XK_Left || key == XK_a || key == XK_A) camera_col -= 4;
        else if (key == XK_Right || key == XK_d || key == XK_D) camera_col += 4;

        clamp_camera();
        mark_all_dirty();
    }

    void handle_event(XEvent& event) {
        switch (event.type) {
            case ConfigureNotify: {
                window_w = max(980, event.xconfigure.width);
                window_h = max(640, event.xconfigure.height);
                rebuild_canvas();
                clamp_camera();
                mark_all_dirty();
                break;
            }
            case Expose:
                mark_all_dirty();
                break;
            case KeyPress:
                handle_key_press(&event.xkey);
                break;
            case MotionNotify: {
                mouse_x = event.xmotion.x;
                mouse_y = event.xmotion.y;
                update_hover();
                if (drag_mode == DragMode::Paint) {
                    int row = 0;
                    int col = 0;
                    if (screen_to_pixel(mouse_x, mouse_y, row, col)) paint_at(row, col, tool == Tool::Eraser);
                } else if (drag_mode == DragMode::Pan) {
                    int dx = event.xmotion.x - pan_anchor_x;
                    int dy = event.xmotion.y - pan_anchor_y;
                    camera_col = pan_start_col - dx / max(1, zoom);
                    camera_row = pan_start_row - dy / max(1, zoom);
                    clamp_camera();
                    mark_all_dirty();
                } else if (drag_mode == DragMode::Rect) {
                    int row = 0;
                    int col = 0;
                    if (screen_to_pixel(mouse_x, mouse_y, row, col)) {
                        rect_end_row = row;
                        rect_end_col = col;
                        mark_canvas_dirty();
                    }
                } else if (drag_mode == DragMode::ItemMove) {
                    int row = 0;
                    int col = 0;
                    if (selected_item >= 0 && screen_to_pixel(mouse_x, mouse_y, row, col)) {
                        items[selected_item].row = clamp_int(row - move_offset_row, 0, terrain.h - 1);
                        items[selected_item].col = clamp_int(col - move_offset_col, 0, terrain.w - 1);
                        mark_all_dirty();
                    }
                } else if (drag_mode == DragMode::SliderR) {
                    adjust_slider("slider_r", mouse_x);
                } else if (drag_mode == DragMode::SliderG) {
                    adjust_slider("slider_g", mouse_x);
                } else if (drag_mode == DragMode::SliderB) {
                    adjust_slider("slider_b", mouse_x);
                }
                break;
            }
            case ButtonPress: {
                mouse_x = event.xbutton.x;
                mouse_y = event.xbutton.y;
                update_hover();

                if (event.xbutton.button == Button4) {
                    zoom = min(48, zoom + 1);
                    clamp_camera();
                    mark_all_dirty();
                    return;
                }
                if (event.xbutton.button == Button5) {
                    zoom = max(2, zoom - 1);
                    clamp_camera();
                    mark_all_dirty();
                    return;
                }
                if (event.xbutton.button == Button2) {
                    drag_mode = DragMode::Pan;
                    pan_anchor_x = event.xbutton.x;
                    pan_anchor_y = event.xbutton.y;
                    pan_start_row = camera_row;
                    pan_start_col = camera_col;
                    return;
                }
                if (event.xbutton.button == Button3) {
                    int row = 0;
                    int col = 0;
                    if (screen_to_pixel(event.xbutton.x, event.xbutton.y, row, col)) {
                        current_color = terrain.get(row, col);
                        current_color.a = 255;
                        mark_panel_dirty();
                    }
                    return;
                }
                if (event.xbutton.button != Button1) return;

                int hit_index = hit_index_at(event.xbutton.x, event.xbutton.y);
                if (hit_index >= 0) {
                    const HitBox hit = hits[hit_index];
                    if (hit.id == "slider_r") {
                        drag_mode = DragMode::SliderR;
                        adjust_slider("slider_r", event.xbutton.x);
                    } else if (hit.id == "slider_g") {
                        drag_mode = DragMode::SliderG;
                        adjust_slider("slider_g", event.xbutton.x);
                    } else if (hit.id == "slider_b") {
                        drag_mode = DragMode::SliderB;
                        adjust_slider("slider_b", event.xbutton.x);
                    } else {
                        handle_panel_action(hit);
                    }
                } else if (event.xbutton.x < canvas_w && event.xbutton.y < canvas_h) {
                    if (input_active) commit_input();
                    begin_canvas_action(event.xbutton.x, event.xbutton.y);
                }
                break;
            }
            case ButtonRelease: {
                if (event.xbutton.button == Button1) {
                    if (drag_mode == DragMode::Rect) {
                        apply_rect_fill();
                        rect_start_row = rect_start_col = rect_end_row = rect_end_col = -1;
                    }
                    if (drag_mode == DragMode::ItemMove) {
                        set_status("Item moved.");
                    }
                    if (drag_mode == DragMode::Paint || drag_mode == DragMode::Rect || drag_mode == DragMode::ItemMove ||
                        drag_mode == DragMode::SliderR || drag_mode == DragMode::SliderG || drag_mode == DragMode::SliderB) {
                        drag_mode = DragMode::Idle;
                    }
                }
                if (event.xbutton.button == Button2 && drag_mode == DragMode::Pan) {
                    drag_mode = DragMode::Idle;
                }
                break;
            }
        }
    }

    void render() {
        if (canvas_dirty) {
            render_canvas();
            XPutImage(display, window, gc, canvas_image, 0, 0, 0, 0, canvas_w, canvas_h);
            canvas_dirty = false;
        }
        if (panel_dirty) {
            draw_panel();
            panel_dirty = false;
        }
        if (hover_row >= 0 && hover_col >= 0) {
            int sx = (hover_col - camera_col) * zoom;
            int sy = (hover_row - camera_row) * zoom;
            if (sx + zoom > 0 && sy + zoom > 0 && sx < canvas_w && sy < canvas_h) {
                XSetForeground(display, gc, 0xFFFFFF);
                XDrawRectangle(display, window, gc, sx, sy, max(1, zoom - 1), max(1, zoom - 1));
            }
        }
        XFlush(display);
        dirty = false;
    }
};

int main(int argc, char** argv) {
    string item_root_arg;
    if (argc >= 2) item_root_arg = argv[1];
    MapEditor editor(item_root_arg);
    editor.run();
    return 0;
}