#include <bits/stdc++.h>
#include <unistd.h>
#include <png.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <dirent.h>
#include <sys/stat.h>
#include "json.hpp"

using namespace std;
using namespace nlohmann;

class Color {
public:
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;

    static Color from_hex(int hex) {
        Color c;
        c.r = (hex >> 16) & 0xFF;
        c.g = (hex >> 8) & 0xFF;
        c.b = hex & 0xFF;
        return c;
    }

    int to_hex() const {
        return (int(r) << 16) | (int(g) << 8) | int(b);
    }
};

class Image {
public:
    int w = 0;
    int h = 0;
    vector<vector<Color>> mat;

    void init(int W, int H) {
        w = W;
        h = H;
        mat.assign(H, vector<Color>(W, Color::from_hex(0xFFFFFF)));
    }
};

bool readPNG(const string& filename, Image& img) {
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp) return false;

    png_byte header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        fclose(fp);
        return false;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);
    int color_type = png_get_color_type(png_ptr, info_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && png_get_bit_depth(png_ptr, info_ptr) < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_RGB_ALPHA || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_strip_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);
    img.init(width, height);

    png_bytep* row_pointers = new png_bytep[height];
    for (int i = 0; i < height; i++) row_pointers[i] = new png_byte[png_get_rowbytes(png_ptr, info_ptr)];
    png_read_image(png_ptr, row_pointers);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            img.mat[y][x].r = row_pointers[y][x * 3];
            img.mat[y][x].g = row_pointers[y][x * 3 + 1];
            img.mat[y][x].b = row_pointers[y][x * 3 + 2];
        }
        delete[] row_pointers[y];
    }
    delete[] row_pointers;

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    fclose(fp);
    return true;
}

struct ItemTemplate {
    string name;
    string path;
    vector<Image> frames;
};

struct ItemInstance {
    int template_idx = 0;
    int frame_idx = 0;
    int x = 0;
    int y = 0;
};

struct CachedItem {
    int template_idx = 0;
    int frame_idx = 0;
};

struct Snapshot {
    int w = 0;
    int h = 0;
    vector<Color> cells;
    vector<ItemInstance> items;
};

class MapData {
public:
    int w = 96;
    int h = 64;
    vector<Color> cells;
    vector<ItemInstance> items;

    MapData() {
        cells.assign(w * h, Color::from_hex(0x1E2B3A));
    }

    bool in_bounds(int x, int y) const {
        return x >= 0 && x < h && y >= 0 && y < w;
    }

    Color get(int x, int y) const {
        return cells[x * w + y];
    }

    void set(int x, int y, const Color& c) {
        if (!in_bounds(x, y)) return;
        cells[x * w + y] = c;
    }

    void clear(const Color& c) {
        fill(cells.begin(), cells.end(), c);
        items.clear();
    }

    void resize_keep(int nw, int nh, const Color& fill_color) {
        nw = max(8, min(512, nw));
        nh = max(8, min(512, nh));
        vector<Color> next(nw * nh, fill_color);

        int hh = min(h, nh);
        int ww = min(w, nw);
        for (int i = 0; i < hh; i++) {
            for (int j = 0; j < ww; j++) {
                next[i * nw + j] = cells[i * w + j];
            }
        }

        w = nw;
        h = nh;
        cells.swap(next);

        vector<ItemInstance> kept;
        for (auto& it : items) {
            if (it.x >= 0 && it.x < h && it.y >= 0 && it.y < w) kept.push_back(it);
        }
        items.swap(kept);
    }

    Snapshot snapshot() const {
        Snapshot s;
        s.w = w;
        s.h = h;
        s.cells = cells;
        s.items = items;
        return s;
    }

    void restore(const Snapshot& s) {
        w = s.w;
        h = s.h;
        cells = s.cells;
        items = s.items;
    }
};

struct UiHit {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    string id;
    int value = 0;
};

class MapEditor {
private:
    Display* dpy = nullptr;
    int scr = 0;
    Window win = 0;
    GC gc = 0;
    XImage* canvas_img = nullptr;
    XFontStruct* font = nullptr;

    int win_w = 1520;
    int win_h = 940;
    int panel_w = 460;
    int canvas_w = 1060;
    int canvas_h = 940;

    int cell_px = 14;
    int camera_x = 0;
    int camera_y = 0;

    bool running = true;
    bool left_down = false;
    bool middle_down = false;

    int mouse_x = 0;
    int mouse_y = 0;
    int pan_last_x = 0;
    int pan_last_y = 0;

    enum Tool {
        TOOL_BRUSH,
        TOOL_ERASER,
        TOOL_FILL,
        TOOL_PICK,
        TOOL_ITEM_PLACE,
        TOOL_ITEM_EDIT
    } tool = TOOL_BRUSH;

    enum DragMode {
        DRAG_NONE,
        DRAG_DRAW,
        DRAG_PAN,
        DRAG_ITEM_MOVE,
        DRAG_R,
        DRAG_G,
        DRAG_B
    } drag_mode = DRAG_NONE;

    MapData map;
    vector<Snapshot> undo_st;
    vector<Snapshot> redo_st;
    static constexpr int MAX_HISTORY = 80;

    vector<ItemTemplate> templates;
    int current_template = 0;
    int current_frame = 0;
    string items_base = "./items";
    vector<string> item_paths;
    int current_item_path = 0;
    vector<CachedItem> item_cache;
    static constexpr int ITEM_CACHE_MAX = 12;
    int item_step = 2;
    int selected_item = -1;
    int move_offset_x = 0;
    int move_offset_y = 0;

    string input_buffer;
    bool input_active = false;
    string input_target;
    int input_box_x = 0, input_box_y = 0, input_box_w = 0, input_box_h = 0;
    bool input_cursor_visible = true;
    int input_cursor_timer = 0;
    string input_status_msg;

    vector<Color> palette;
    int palette_idx = 0;
    Color current_color = Color::from_hex(0x4D648D);
    bool show_grid = true;
    int brush_radius = 2;

    vector<UiHit> hits;
    bool needs_redraw = true;
    bool panel_dirty = true;
    bool canvas_full_dirty = true;
    bool canvas_part_dirty = false;
    int dirty_x1 = 0;
    int dirty_y1 = 0;
    int dirty_x2 = 0;
    int dirty_y2 = 0;
    int hover_cx = -1;
    int hover_cy = -1;

private:
    unsigned long px(const Color& c) const {
        return (static_cast<unsigned long>(c.r) << 16) |
               (static_cast<unsigned long>(c.g) << 8) |
               static_cast<unsigned long>(c.b);
    }

    static bool is_dir(const string& path) {
        struct stat st {};
        if (stat(path.c_str(), &st) != 0) return false;
        return S_ISDIR(st.st_mode);
    }

    bool has_template_dir(const string& base) const {
        DIR* dir = opendir(base.c_str());
        if (!dir) return false;
        dirent* ent;
        bool ok = false;
        while ((ent = readdir(dir)) != nullptr) {
            string name = ent->d_name;
            if (name == "." || name == "..") continue;
            string full = base + "/" + name;
            if (!is_dir(full)) continue;
            string first = full + "/1.png";
            if (access(first.c_str(), F_OK) == 0) {
                ok = true;
                break;
            }
        }
        closedir(dir);
        return ok;
    }

    static void append_unique(vector<string>& arr, const string& path) {
        for (auto& it : arr) {
            if (it == path) return;
        }
        arr.push_back(path);
    }

    vector<string> discover_item_paths(const string& prefer) const {
        vector<string> out;
        if (!prefer.empty()) append_unique(out, prefer);
        append_unique(out, "./items");

        DIR* dir = opendir(".");
        if (dir) {
            dirent* ent;
            while ((ent = readdir(dir)) != nullptr) {
                string name = ent->d_name;
                if (name == "." || name == "..") continue;
                string full = "./" + name;
                if (!is_dir(full)) continue;
                if (has_template_dir(full)) append_unique(out, full);
            }
            closedir(dir);
        }

        vector<string> filtered;
        for (auto& p : out) {
            if (has_template_dir(p)) filtered.push_back(p);
        }
        if (filtered.empty()) filtered.push_back(prefer.empty() ? "./items" : prefer);
        return filtered;
    }

    void init_palette() {
        palette.clear();
        palette.push_back(Color::from_hex(0x1E2B3A));
        palette.push_back(Color::from_hex(0x2E4A7D));
        palette.push_back(Color::from_hex(0x4D648D));
        palette.push_back(Color::from_hex(0x78909C));
        palette.push_back(Color::from_hex(0xA3B18A));
        palette.push_back(Color::from_hex(0xD4A373));
        palette.push_back(Color::from_hex(0xE76F51));
        palette.push_back(Color::from_hex(0xE63946));
        palette.push_back(Color::from_hex(0x7B2CBF));
        palette.push_back(Color::from_hex(0xF4D35E));
        palette.push_back(Color::from_hex(0xF1FAEE));
        palette.push_back(Color::from_hex(0x111111));
        current_color = palette[2];
    }

    void create_canvas_image() {
        if (canvas_img) {
            XDestroyImage(canvas_img);
            canvas_img = nullptr;
        }
        char* data = (char*)malloc(canvas_w * canvas_h * 4);
        canvas_img = XCreateImage(
            dpy,
            DefaultVisual(dpy, scr),
            DefaultDepth(dpy, scr),
            ZPixmap,
            0,
            data,
            canvas_w,
            canvas_h,
            32,
            0
        );
    }

    void set_canvas_px(int x, int y, unsigned long c) {
        if (!canvas_img) return;
        if (x < 0 || x >= canvas_w || y < 0 || y >= canvas_h) return;
        auto* row = reinterpret_cast<uint32_t*>(canvas_img->data + y * canvas_img->bytes_per_line);
        row[x] = static_cast<uint32_t>(c);
    }

    void fill_canvas_rect(int x, int y, int w, int h, unsigned long c) {
        int x1 = max(0, x), y1 = max(0, y);
        int x2 = min(canvas_w, x + w), y2 = min(canvas_h, y + h);
        uint32_t cc = static_cast<uint32_t>(c);
        for (int yy = y1; yy < y2; yy++) {
            auto* row = reinterpret_cast<uint32_t*>(canvas_img->data + yy * canvas_img->bytes_per_line);
            for (int xx = x1; xx < x2; xx++) row[xx] = cc;
        }
    }

    void clamp_camera() {
        int vr = max(1, canvas_h / max(1, cell_px));
        int vc = max(1, canvas_w / max(1, cell_px));
        camera_x = max(0, min(camera_x, max(0, map.h - vr)));
        camera_y = max(0, min(camera_y, max(0, map.w - vc)));
    }

    void mark_panel_dirty() {
        panel_dirty = true;
        needs_redraw = true;
    }

    void mark_canvas_full_dirty() {
        canvas_full_dirty = true;
        canvas_part_dirty = false;
        needs_redraw = true;
    }

    void mark_canvas_part_dirty(int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) return;
        int x1 = max(0, x);
        int y1 = max(0, y);
        int x2 = min(canvas_w, x + w);
        int y2 = min(canvas_h, y + h);
        if (x1 >= x2 || y1 >= y2) return;
        if (!canvas_part_dirty) {
            dirty_x1 = x1;
            dirty_y1 = y1;
            dirty_x2 = x2;
            dirty_y2 = y2;
            canvas_part_dirty = true;
        } else {
            dirty_x1 = min(dirty_x1, x1);
            dirty_y1 = min(dirty_y1, y1);
            dirty_x2 = max(dirty_x2, x2);
            dirty_y2 = max(dirty_y2, y2);
        }
        needs_redraw = true;
    }

    void push_undo() {
        undo_st.push_back(map.snapshot());
        if ((int)undo_st.size() > MAX_HISTORY) undo_st.erase(undo_st.begin());
        redo_st.clear();
    }

    void undo() {
        if (undo_st.empty()) return;
        redo_st.push_back(map.snapshot());
        map.restore(undo_st.back());
        undo_st.pop_back();
        selected_item = -1;
        clamp_camera();
    }

    void redo() {
        if (redo_st.empty()) return;
        undo_st.push_back(map.snapshot());
        map.restore(redo_st.back());
        redo_st.pop_back();
        selected_item = -1;
        clamp_camera();
    }

    bool mouse_to_cell(int sx, int sy, int& mx, int& my) const {
        if (sx < 0 || sx >= canvas_w || sy < 0 || sy >= canvas_h) return false;
        mx = camera_x + sy / max(1, cell_px);
        my = camera_y + sx / max(1, cell_px);
        return map.in_bounds(mx, my);
    }

    string now_string() const {
        time_t t = time(nullptr);
        tm lt{};
        localtime_r(&t, &lt);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &lt);
        return string(buf);
    }

    vector<ItemTemplate> load_templates(const string& base) {
        vector<ItemTemplate> out;
        DIR* dir = opendir(base.c_str());
        if (!dir) return out;

        dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            string name = ent->d_name;
            if (name == "." || name == "..") continue;

            string full = base + "/" + name;
            if (!is_dir(full)) continue;

            ItemTemplate tp;
            tp.name = name;
            tp.path = full;

            for (int i = 1; i <= 128; i++) {
                string file = full + "/" + to_string(i) + ".png";
                if (access(file.c_str(), F_OK) != 0) break;
                Image img;
                if (readPNG(file, img)) tp.frames.push_back(img);
            }
            if (!tp.frames.empty()) out.push_back(tp);
        }

        closedir(dir);
        sort(out.begin(), out.end(), [](const ItemTemplate& a, const ItemTemplate& b) { return a.name < b.name; });
        return out;
    }

    bool item_contains_cell(const ItemInstance& it, int cx, int cy) const {
        if (it.template_idx < 0 || it.template_idx >= (int)templates.size()) return false;
        const auto& tp = templates[it.template_idx];
        if (tp.frames.empty()) return false;
        int fi = max(0, min((int)tp.frames.size() - 1, it.frame_idx));
        const Image& img = tp.frames[fi];
        return cx >= it.x && cx < it.x + img.h && cy >= it.y && cy < it.y + img.w;
    }

    int find_item_at(int cx, int cy) const {
        for (int i = (int)map.items.size() - 1; i >= 0; i--) {
            if (item_contains_cell(map.items[i], cx, cy)) return i;
        }
        return -1;
    }

    void update_item_cache(int template_idx, int frame_idx) {
        CachedItem c;
        c.template_idx = template_idx;
        c.frame_idx = frame_idx;

        vector<CachedItem> next;
        next.push_back(c);
        for (auto& one : item_cache) {
            if (one.template_idx == c.template_idx && one.frame_idx == c.frame_idx) continue;
            next.push_back(one);
            if ((int)next.size() >= ITEM_CACHE_MAX) break;
        }
        item_cache.swap(next);
    }

    void use_cached_item(int idx) {
        if (idx < 0 || idx >= (int)item_cache.size()) return;
        const auto& c = item_cache[idx];
        if (c.template_idx < 0 || c.template_idx >= (int)templates.size()) return;
        const auto& tp = templates[c.template_idx];
        if (tp.frames.empty()) return;
        current_template = c.template_idx;
        current_frame = max(0, min((int)tp.frames.size() - 1, c.frame_idx));
        update_item_cache(current_template, current_frame);
    }

    void sanitize_item_cache() {
        vector<CachedItem> next;
        for (auto& c : item_cache) {
            if (c.template_idx < 0 || c.template_idx >= (int)templates.size()) continue;
            const auto& tp = templates[c.template_idx];
            if (tp.frames.empty()) continue;
            CachedItem one = c;
            one.frame_idx = max(0, min((int)tp.frames.size() - 1, one.frame_idx));
            bool dup = false;
            for (auto& ex : next) {
                if (ex.template_idx == one.template_idx && ex.frame_idx == one.frame_idx) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            next.push_back(one);
            if ((int)next.size() >= ITEM_CACHE_MAX) break;
        }
        item_cache.swap(next);
    }

    void apply_item_path_index(int idx) {
        if (item_paths.empty()) return;
        current_item_path = (idx % (int)item_paths.size() + (int)item_paths.size()) % (int)item_paths.size();
        items_base = item_paths[current_item_path];
        templates = load_templates(items_base);
        current_template = 0;
        current_frame = 0;
        sanitize_item_cache();
        selected_item = -1;
    }

    void draw_stroke(int sx, int sy, bool erase) {
        int mx, my;
        if (!mouse_to_cell(sx, sy, mx, my)) return;
        Color c = erase ? palette[0] : current_color;
        int rad = max(1, brush_radius) - 1;
        bool any_change = false;
        for (int dx = -rad; dx <= rad; dx++) {
            for (int dy = -rad; dy <= rad; dy++) {
                if (dx * dx + dy * dy > rad * rad) continue;
                int tx = mx + dx, ty = my + dy;
                if (!map.in_bounds(tx, ty)) continue;
                Color old = map.get(tx, ty);
                if (old.r == c.r && old.g == c.g && old.b == c.b) continue;
                map.set(tx, ty, c);
                render_cell_to_canvas(tx, ty);
                any_change = true;
            }
        }
        if (any_change) mark_panel_dirty();
    }

    void flood_fill(int sx, int sy) {
        int mx, my;
        if (!mouse_to_cell(sx, sy, mx, my)) return;
        Color src = map.get(mx, my);
        Color dst = current_color;
        if (src.r == dst.r && src.g == dst.g && src.b == dst.b) return;

        queue<pair<int, int>> q;
        vector<char> vis(map.w * map.h, 0);
        auto same = [](const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b;
        };

        q.push({mx, my});
        vis[mx * map.w + my] = 1;
        while (!q.empty()) {
            auto [x, y] = q.front();
            q.pop();
            map.set(x, y, dst);
            const int dx[4] = {1, -1, 0, 0};
            const int dy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++) {
                int nx = x + dx[k], ny = y + dy[k];
                if (!map.in_bounds(nx, ny)) continue;
                int id = nx * map.w + ny;
                if (vis[id]) continue;
                if (!same(map.get(nx, ny), src)) continue;
                vis[id] = 1;
                q.push({nx, ny});
            }
        }
    }

    void pick_color(int sx, int sy) {
        int mx, my;
        if (!mouse_to_cell(sx, sy, mx, my)) return;
        current_color = map.get(mx, my);
    }

    void place_item(int sx, int sy) {
        if (templates.empty()) return;
        int mx, my;
        if (!mouse_to_cell(sx, sy, mx, my)) return;
        int step = max(1, item_step);
        int gx = (mx / step) * step;
        int gy = (my / step) * step;

        ItemInstance it;
        it.template_idx = max(0, min((int)templates.size() - 1, current_template));
        it.frame_idx = max(0, min((int)templates[it.template_idx].frames.size() - 1, current_frame));
        it.x = gx;
        it.y = gy;
        map.items.push_back(it);
        update_item_cache(it.template_idx, it.frame_idx);
        selected_item = (int)map.items.size() - 1;
    }

    void copy_selected_item() {
        if (selected_item < 0 || selected_item >= (int)map.items.size()) return;
        ItemInstance cp = map.items[selected_item];
        cp.x = min(map.h - 1, cp.x + max(1, item_step));
        cp.y = min(map.w - 1, cp.y + max(1, item_step));
        map.items.push_back(cp);
        selected_item = (int)map.items.size() - 1;
    }

    void delete_selected_item() {
        if (selected_item < 0 || selected_item >= (int)map.items.size()) return;
        map.items.erase(map.items.begin() + selected_item);
        selected_item = -1;
    }

    bool export_json(const string& file) {
        json j;
        j["meta"] = {
            {"editor", "map_editor.cpp"},
            {"timestamp", (long long)time(nullptr)},
            {"note", "mouse-first map editor"}
        };

        j["map"] = {
            {"width", map.w},
            {"height", map.h},
            {"cell_px", cell_px},
            {"camera_x", camera_x},
            {"camera_y", camera_y}
        };

        vector<vector<int>> data(map.h, vector<int>(map.w, 0));
        for (int i = 0; i < map.h; i++) {
            for (int k = 0; k < map.w; k++) {
                data[i][k] = map.get(i, k).to_hex();
            }
        }
        j["map"]["data"] = data;

        j["editor"] = {
            {"show_grid", show_grid},
            {"brush_radius", brush_radius},
            {"item_step", item_step},
            {"current_color", current_color.to_hex()},
            {"current_template", current_template},
            {"current_frame", current_frame},
            {"item_base_path", items_base}
        };

        json templates_json = json::array();
        for (auto& tp : templates) {
            templates_json.push_back({
                {"name", tp.name},
                {"path", tp.path},
                {"frames", (int)tp.frames.size()}
            });
        }
        j["item_templates"] = templates_json;

        json instances = json::array();
        for (auto& it : map.items) {
            instances.push_back({
                {"template_idx", it.template_idx},
                {"frame_idx", it.frame_idx},
                {"x", it.x},
                {"y", it.y}
            });
        }
        j["item_instances"] = instances;

        json cache_json = json::array();
        for (auto& c : item_cache) {
            cache_json.push_back({
                {"template_idx", c.template_idx},
                {"frame_idx", c.frame_idx}
            });
        }
        j["item_cache"] = cache_json;

        ofstream out(file);
        if (!out.is_open()) return false;
        out << j.dump(2);
        return true;
    }

    bool import_json(const string& file) {
        ifstream in(file);
        if (!in.is_open()) return false;

        json j;
        try {
            in >> j;
            in.close();

            if (j.contains("map")) {
                auto jm = j["map"];
                int nw = jm.value("width", map.w);
                int nh = jm.value("height", map.h);
                map.resize_keep(nw, nh, palette[0]);

                if (jm.contains("data")) {
                    auto d = jm["data"];
                    for (int i = 0; i < map.h && i < (int)d.size(); i++) {
                        for (int k = 0; k < map.w && k < (int)d[i].size(); k++) {
                            map.set(i, k, Color::from_hex((int)d[i][k]));
                        }
                    }
                }
                cell_px = jm.value("cell_px", cell_px);
                camera_x = jm.value("camera_x", camera_x);
                camera_y = jm.value("camera_y", camera_y);
                clamp_camera();
            }

            if (j.contains("editor")) {
                auto je = j["editor"];
                show_grid = je.value("show_grid", show_grid);
                brush_radius = je.value("brush_radius", brush_radius);
                item_step = je.value("item_step", item_step);
                current_color = Color::from_hex(je.value("current_color", current_color.to_hex()));
                string import_base = je.value("item_base_path", items_base);
                if (!import_base.empty()) {
                    append_unique(item_paths, import_base);
                    int idx = 0;
                    for (int i = 0; i < (int)item_paths.size(); i++) {
                        if (item_paths[i] == import_base) {
                            idx = i;
                            break;
                        }
                    }
                    apply_item_path_index(idx);
                }
                current_template = je.value("current_template", current_template);
                current_frame = je.value("current_frame", current_frame);
            }

            map.items.clear();
            if (j.contains("item_instances")) {
                for (auto& one : j["item_instances"]) {
                    ItemInstance it;
                    it.template_idx = one.value("template_idx", 0);
                    it.frame_idx = one.value("frame_idx", 0);
                    it.x = one.value("x", 0);
                    it.y = one.value("y", 0);
                    if (map.in_bounds(it.x, it.y)) map.items.push_back(it);
                }
            }

            item_cache.clear();
            if (j.contains("item_cache")) {
                for (auto& one : j["item_cache"]) {
                    CachedItem c;
                    c.template_idx = one.value("template_idx", 0);
                    c.frame_idx = one.value("frame_idx", 0);
                    item_cache.push_back(c);
                }
            }
            sanitize_item_cache();

            if (!templates.empty()) {
                current_template = max(0, min((int)templates.size() - 1, current_template));
                int n = templates[current_template].frames.size();
                current_frame = n > 0 ? max(0, min(n - 1, current_frame)) : 0;
            } else {
                current_template = 0;
                current_frame = 0;
            }

            selected_item = -1;
            return true;
        } catch (...) {
            return false;
        }
    }

    void cancel_input() {
        input_active = false;
        input_buffer.clear();
        input_target.clear();
        input_status_msg.clear();
    }

    void commit_input() {
        if (input_target == "item_path") {
            string path = input_buffer;
            if (!path.empty() && path.back() == '/') path.pop_back();
            if (path.empty()) {
                input_status_msg = "[Error] Empty path";
                return;
            }
            if (!is_dir(path)) {
                input_status_msg = "[Error] Not a directory: " + path;
                return;
            }
            append_unique(item_paths, path);
            for (int i = 0; i < (int)item_paths.size(); i++) {
                if (item_paths[i] == path) {
                    apply_item_path_index(i);
                    input_status_msg = "[OK] Path set: " + path;
                    break;
                }
            }
        }
        input_active = false;
        input_buffer.clear();
        input_target.clear();
    }

    void handle_input_key(XKeyEvent* e) {
        KeySym ks = XLookupKeysym(e, 0);
        if (ks == XK_Return || ks == XK_KP_Enter) {
            commit_input();
            return;
        }
        if (ks == XK_Escape) {
            cancel_input();
            return;
        }
        if (ks == XK_BackSpace) {
            if (!input_buffer.empty()) input_buffer.pop_back();
            return;
        }
        if (ks == XK_Delete) {
            return;
        }
        if (ks == XK_Home || ks == XK_End || ks == XK_Left || ks == XK_Right) {
            return;
        }
        char buf[32];
        Status status;
        int len = XLookupString(e, buf, sizeof(buf) - 1, nullptr, nullptr);
        if (len > 0) {
            buf[len] = '\0';
            string s(buf);
            for (char c : s) {
                if (c >= 32 && c < 127) {
                    input_buffer += c;
                }
            }
        }
    }

    void panel_text_input(int x, int y, int w, int h, const string& id, const string& display, bool active) {
        XSetForeground(dpy, gc, 0x13263D);
        XFillRectangle(dpy, win, gc, x, y, w, h);
        XSetForeground(dpy, gc, active ? 0x6EA8FF : 0x375477);
        XDrawRectangle(dpy, win, gc, x, y, w, h);
        string show = display;
        if ((int)show.size() > 50) show = show.substr(0, 50) + "...";
        draw_text(x + 6, y + h / 2 + 5, show, active ? 0xFFFFFF : 0x9DC5EA);
        if (active && input_cursor_visible) {
            int tw = XTextWidth(font, show.c_str(), (int)show.size());
            int cx = x + 8 + tw;
            XSetForeground(dpy, gc, 0xFFFFFF);
            XFillRectangle(dpy, win, gc, cx, y + 3, 2, h - 6);
        }
        hits.push_back({x, y, w, h, id, 0});
    }

    void draw_text(int x, int y, const string& s, unsigned long col) {
        XSetForeground(dpy, gc, col);
        XDrawString(dpy, win, gc, x, y, s.c_str(), (int)s.size());
    }

    void panel_button(int x, int y, int w, int h, const string& label, const string& id, bool active = false, int value = 0) {
        XSetForeground(dpy, gc, active ? 0x255B9F : 0x1A2A3D);
        XFillRectangle(dpy, win, gc, x, y, w, h);
        XSetForeground(dpy, gc, active ? 0x9CC9FF : 0x375477);
        XDrawRectangle(dpy, win, gc, x, y, w, h);
        draw_text(x + 8, y + h / 2 + 5, label, 0xE6F1FF);
        hits.push_back({x, y, w, h, id, value});
    }

    void color_swatch(int x, int y, int w, int h, const Color& c, int idx) {
        XSetForeground(dpy, gc, px(c));
        XFillRectangle(dpy, win, gc, x, y, w, h);
        XSetForeground(dpy, gc, idx == palette_idx ? 0xFFFFFF : 0x22354D);
        XDrawRectangle(dpy, win, gc, x, y, w, h);
        hits.push_back({x, y, w, h, "palette", idx});
    }

    void slider(int x, int y, int w, int h, int v, const string& id) {
        XSetForeground(dpy, gc, 0x213347);
        XFillRectangle(dpy, win, gc, x, y, w, h);
        XSetForeground(dpy, gc, 0x6EA8FF);
        int fill_w = max(0, min(w, v * w / 255));
        XFillRectangle(dpy, win, gc, x, y, fill_w, h);
        XSetForeground(dpy, gc, 0xDCEEFF);
        int knob = x + fill_w - 2;
        XFillRectangle(dpy, win, gc, knob, y - 2, 4, h + 4);
        hits.push_back({x, y, w, h, id, 0});
    }

    bool top_item_color_at_cell(int mx, int my, unsigned long& out_col) const {
        for (int idx = (int)map.items.size() - 1; idx >= 0; idx--) {
            const auto& it = map.items[idx];
            if (it.template_idx < 0 || it.template_idx >= (int)templates.size()) continue;
            const auto& tp = templates[it.template_idx];
            if (tp.frames.empty()) continue;
            int f = max(0, min((int)tp.frames.size() - 1, it.frame_idx));
            const Image& img = tp.frames[f];
            int rx = mx - it.x;
            int ry = my - it.y;
            if (rx < 0 || ry < 0 || rx >= img.h || ry >= img.w) continue;
            const Color& c = img.mat[rx][ry];
            if (c.r == 255 && c.g == 255 && c.b == 255) continue;
            out_col = px(c);
            return true;
        }
        return false;
    }

    void render_cell_to_canvas(int mx, int my) {
        if (!map.in_bounds(mx, my)) return;
        int sxp = (my - camera_y) * cell_px;
        int syp = (mx - camera_x) * cell_px;
        if (sxp + cell_px <= 0 || syp + cell_px <= 0 || sxp >= canvas_w || syp >= canvas_h) return;

        fill_canvas_rect(sxp, syp, cell_px, cell_px, px(map.get(mx, my)));
        if (show_grid && cell_px >= 6) {
            fill_canvas_rect(sxp, syp, cell_px, 1, 0x2A3442);
            fill_canvas_rect(sxp, syp, 1, cell_px, 0x2A3442);
        }

        unsigned long item_col = 0;
        if (top_item_color_at_cell(mx, my, item_col)) {
            fill_canvas_rect(sxp, syp, cell_px, cell_px, item_col);
        }
        mark_canvas_part_dirty(sxp, syp, cell_px, cell_px);
    }

    void redraw_canvas_full() {
        for (int y = 0; y < canvas_h; y++) {
            double t = double(y) / max(1, canvas_h - 1);
            int r = int(18 + 10 * t);
            int g = int(30 + 12 * t);
            int b = int(45 + 14 * t);
            unsigned long c = (static_cast<unsigned long>(r) << 16) |
                              (static_cast<unsigned long>(g) << 8) |
                              static_cast<unsigned long>(b);
            fill_canvas_rect(0, y, canvas_w, 1, c);
        }

        int vis_r = canvas_h / max(1, cell_px) + 2;
        int vis_c = canvas_w / max(1, cell_px) + 2;
        int sx = camera_x;
        int ex = min(map.h, camera_x + vis_r);
        int sy = camera_y;
        int ey = min(map.w, camera_y + vis_c);

        for (int i = sx; i < ex; i++) {
            for (int j = sy; j < ey; j++) {
                int px0 = (j - camera_y) * cell_px;
                int py0 = (i - camera_x) * cell_px;
                fill_canvas_rect(px0, py0, cell_px, cell_px, px(map.get(i, j)));
                if (show_grid && cell_px >= 6) {
                    fill_canvas_rect(px0, py0, cell_px, 1, 0x2A3442);
                    fill_canvas_rect(px0, py0, 1, cell_px, 0x2A3442);
                }
            }
        }

        for (int idx = 0; idx < (int)map.items.size(); idx++) {
            auto& it = map.items[idx];
            if (it.template_idx < 0 || it.template_idx >= (int)templates.size()) continue;
            auto& tp = templates[it.template_idx];
            if (tp.frames.empty()) continue;
            int f = max(0, min((int)tp.frames.size() - 1, it.frame_idx));
            const Image& img = tp.frames[f];
            for (int x = 0; x < img.h; x++) {
                for (int y = 0; y < img.w; y++) {
                    Color c = img.mat[x][y];
                    if (c.r == 255 && c.g == 255 && c.b == 255) continue;
                    int mapx = it.x + x;
                    int mapy = it.y + y;
                    int sxp = (mapy - camera_y) * cell_px;
                    int syp = (mapx - camera_x) * cell_px;
                    if (sxp + cell_px <= 0 || syp + cell_px <= 0 || sxp >= canvas_w || syp >= canvas_h) continue;
                    fill_canvas_rect(sxp, syp, cell_px, cell_px, px(c));
                }
            }

            if (idx == selected_item) {
                int bx = (it.y - camera_y) * cell_px;
                int by = (it.x - camera_x) * cell_px;
                int bw = max(1, img.w * cell_px);
                int bh = max(1, img.h * cell_px);
                fill_canvas_rect(bx, by, bw, 2, 0xFFFFFF);
                fill_canvas_rect(bx, by + bh - 2, bw, 2, 0xFFFFFF);
                fill_canvas_rect(bx, by, 2, bh, 0xFFFFFF);
                fill_canvas_rect(bx + bw - 2, by, 2, bh, 0xFFFFFF);
            }
        }
    }

    void draw_canvas_cursor_hint() {
        if (mouse_x < 0 || mouse_x >= canvas_w || mouse_y < 0 || mouse_y >= canvas_h) return;
        int gx = (mouse_x / max(1, cell_px)) * cell_px;
        int gy = (mouse_y / max(1, cell_px)) * cell_px;
        XSetForeground(dpy, gc, 0xFFFFFF);
        XDrawRectangle(dpy, win, gc, gx, gy, max(1, cell_px - 1), max(1, cell_px - 1));
    }

    void flush_canvas_dirty() {
        if (canvas_full_dirty) {
            redraw_canvas_full();
            XPutImage(dpy, win, gc, canvas_img, 0, 0, 0, 0, canvas_w, canvas_h);
            canvas_full_dirty = false;
            canvas_part_dirty = false;
            return;
        }
        if (canvas_part_dirty) {
            int w = dirty_x2 - dirty_x1;
            int h = dirty_y2 - dirty_y1;
            XPutImage(dpy, win, gc, canvas_img, dirty_x1, dirty_y1, dirty_x1, dirty_y1, w, h);
            canvas_part_dirty = false;
        }
    }

    string tool_name() const {
        switch (tool) {
            case TOOL_BRUSH: return "Brush";
            case TOOL_ERASER: return "Eraser";
            case TOOL_FILL: return "Fill";
            case TOOL_PICK: return "Picker";
            case TOOL_ITEM_PLACE: return "Item Place";
            default: return "Item Edit";
        }
    }

    void draw_panel() {
        hits.clear();
        int px0 = canvas_w;
        XSetForeground(dpy, gc, 0x0C1626);
        XFillRectangle(dpy, win, gc, px0, 0, panel_w, win_h);
        XSetForeground(dpy, gc, 0x22354D);
        XDrawLine(dpy, win, gc, px0, 0, px0, win_h);

        XSetForeground(dpy, gc, 0x13263D);
        XFillRectangle(dpy, win, gc, px0 + 14, 14, panel_w - 28, 74);
        draw_text(px0 + 24, 40, "MAP EDITOR - Mouse First", 0xECF5FF);
        draw_text(px0 + 24, 62, "No flicker | RGB picker | full item workflow", 0xAFCBEA);
        draw_text(px0 + 24, 82, "Import/Move/Copy/Delete all on mouse", 0x8CB0D6);

        int y = 110;
        draw_text(px0 + 24, y, "Tool", 0xECF5FF);
        y += 14;
        panel_button(px0 + 24, y, 92, 30, "Brush", "tool", tool == TOOL_BRUSH, TOOL_BRUSH);
        panel_button(px0 + 124, y, 92, 30, "Eraser", "tool", tool == TOOL_ERASER, TOOL_ERASER);
        panel_button(px0 + 224, y, 92, 30, "Fill", "tool", tool == TOOL_FILL, TOOL_FILL);
        panel_button(px0 + 324, y, 112, 30, "Picker", "tool", tool == TOOL_PICK, TOOL_PICK);
        y += 38;
        panel_button(px0 + 24, y, 192, 30, "Item Place", "tool", tool == TOOL_ITEM_PLACE, TOOL_ITEM_PLACE);
        panel_button(px0 + 224, y, 212, 30, "Item Edit", "tool", tool == TOOL_ITEM_EDIT, TOOL_ITEM_EDIT);

        y += 48;
        draw_text(px0 + 24, y, "Color Picker (Palette + RGB Sliders)", 0xECF5FF);
        y += 10;
        for (int i = 0; i < (int)palette.size(); i++) {
            int x = px0 + 24 + (i % 6) * 68;
            int yy = y + (i / 6) * 26;
            color_swatch(x, yy, 60, 20, palette[i], i);
        }
        y += 58;

        XSetForeground(dpy, gc, px(current_color));
        XFillRectangle(dpy, win, gc, px0 + 24, y, 80, 44);
        XSetForeground(dpy, gc, 0xD5EAFF);
        XDrawRectangle(dpy, win, gc, px0 + 24, y, 80, 44);

        draw_text(px0 + 114, y + 12, "R", 0xFFB7B7);
        slider(px0 + 134, y + 2, 300, 12, current_color.r, "slider_r");
        draw_text(px0 + 114, y + 27, "G", 0xB9FFC2);
        slider(px0 + 134, y + 17, 300, 12, current_color.g, "slider_g");
        draw_text(px0 + 114, y + 42, "B", 0xBDD3FF);
        slider(px0 + 134, y + 32, 300, 12, current_color.b, "slider_b");

        y += 64;
        draw_text(px0 + 24, y, "Canvas / Map", 0xECF5FF);
        y += 8;
        panel_button(px0 + 24, y, 90, 28, "Zoom +", "zoom_in");
        panel_button(px0 + 120, y, 90, 28, "Zoom -", "zoom_out");
        panel_button(px0 + 216, y, 90, 28, "Brush +", "brush_in");
        panel_button(px0 + 312, y, 124, 28, "Brush -", "brush_out");
        y += 34;
        panel_button(px0 + 24, y, 90, 28, "Step +", "step_in");
        panel_button(px0 + 120, y, 90, 28, "Step -", "step_out");
        panel_button(px0 + 216, y, 90, 28, "Grid", "toggle_grid", show_grid);
        panel_button(px0 + 312, y, 124, 28, "Clear", "clear_map");
        y += 34;
        panel_button(px0 + 24, y, 90, 28, "W +", "map_w_in");
        panel_button(px0 + 120, y, 90, 28, "W -", "map_w_out");
        panel_button(px0 + 216, y, 90, 28, "H +", "map_h_in");
        panel_button(px0 + 312, y, 124, 28, "H -", "map_h_out");

        y += 42;
        draw_text(px0 + 24, y, "Item System", 0xECF5FF);
        y += 8;
        panel_button(px0 + 24, y, 80, 28, "Path <", "path_prev");
        panel_button(px0 + 110, y, 80, 28, "Path >", "path_next");
        panel_button(px0 + 196, y, 120, 28, "Reload", "reload_items");
        panel_button(px0 + 322, y, 114, 28, "Use Cache", "cache_use_top");
        y += 34;
        string path_display = input_active ? input_buffer : ("Path: " + items_base);
        panel_text_input(px0 + 24, y, 412, 24, "item_path", path_display, input_active);
        y += 30;
        if (!input_status_msg.empty()) {
            unsigned long col = input_status_msg.rfind("[Error]", 0) == 0 ? 0xFF8B8B : 0x8BFF8B;
            draw_text(px0 + 24, y + 14, input_status_msg, col);
            y += 20;
        }
        panel_button(px0 + 24, y, 140, 28, "Tpl <", "tpl_prev");
        panel_button(px0 + 170, y, 140, 28, "Tpl >", "tpl_next");
        panel_button(px0 + 316, y, 58, 28, "<", "frame_prev");
        panel_button(px0 + 378, y, 58, 28, ">", "frame_next");
        y += 34;
        panel_button(px0 + 24, y, 140, 28, "Copy Item", "copy_item");
        panel_button(px0 + 170, y, 140, 28, "Delete Item", "delete_item");
        panel_button(px0 + 316, y, 120, 28, "Unselect", "unselect_item");

        y += 40;
        draw_text(px0 + 24, y, "Item Cache (click to reuse)", 0xECF5FF);
        y += 8;
        int show_n = min(5, (int)item_cache.size());
        for (int i = 0; i < show_n; i++) {
            const auto& c = item_cache[i];
            string label = "#" + to_string(i + 1) + " ";
            if (c.template_idx >= 0 && c.template_idx < (int)templates.size()) {
                label += templates[c.template_idx].name + " f" + to_string(c.frame_idx + 1);
            } else {
                label += "(invalid)";
            }
            panel_button(px0 + 24, y, 412, 22, label, "cache_item", i == 0, i);
            y += 24;
        }
        if (show_n == 0) {
            draw_text(px0 + 24, y + 16, "(empty: place item to fill cache)", 0x89AED1);
            y += 24;
        }

        y += 12;
        draw_text(px0 + 24, y, "File", 0xECF5FF);
        y += 8;
        panel_button(px0 + 24, y, 140, 28, "Export JSON", "export_json");
        panel_button(px0 + 170, y, 140, 28, "Import JSON", "import_json");
        panel_button(px0 + 316, y, 120, 28, "Undo", "undo");
        y += 34;
        panel_button(px0 + 316, y, 120, 28, "Redo", "redo");

        y += 38;
        string tpl_name = templates.empty() ? "(none)" : templates[current_template].name;
        string line1 = "Tool=" + tool_name() + "  Map=" + to_string(map.w) + "x" + to_string(map.h) + "  Zoom=" + to_string(cell_px);
        string line2 = "Brush=" + to_string(brush_radius) + "  Step=" + to_string(item_step) + "  Items=" + to_string(map.items.size());
        string line3 = "Template=" + tpl_name + "  Frame=" + to_string(current_frame + 1);
        string line4 = "SelectedItem=" + to_string(selected_item) + "  Paths=" + to_string(item_paths.size());
        draw_text(px0 + 24, y, line1, 0xB7D2F0);
        draw_text(px0 + 24, y + 20, line2, 0xB7D2F0);
        draw_text(px0 + 24, y + 40, line3, 0xB7D2F0);
        draw_text(px0 + 24, y + 60, line4, 0xB7D2F0);

        draw_text(px0 + 24, win_h - 18, "LMB draw/place, MMB pan, wheel zoom, RMB pick", 0x8FB5D9);
    }

    int hit_index(int x, int y) const {
        for (int i = (int)hits.size() - 1; i >= 0; i--) {
            const auto& h = hits[i];
            if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return i;
        }
        return -1;
    }

    void slider_update(const string& id, int mx) {
        for (auto& h : hits) {
            if (h.id != id) continue;
            int v = (mx - h.x) * 255 / max(1, h.w);
            v = max(0, min(255, v));
            if (id == "slider_r") current_color.r = (unsigned char)v;
            if (id == "slider_g") current_color.g = (unsigned char)v;
            if (id == "slider_b") current_color.b = (unsigned char)v;
            return;
        }
    }

    void panel_action(const UiHit& h) {
        input_status_msg.clear();
        if (h.id == "tool") {
            tool = (Tool)h.value;
            mark_panel_dirty();
            return;
        }
        if (h.id == "palette") {
            palette_idx = h.value;
            if (palette_idx >= 0 && palette_idx < (int)palette.size()) current_color = palette[palette_idx];
            mark_panel_dirty();
            return;
        }

        if (h.id == "zoom_in") cell_px = min(40, cell_px + 1);
        else if (h.id == "zoom_out") cell_px = max(4, cell_px - 1);
        else if (h.id == "brush_in") brush_radius = min(30, brush_radius + 1);
        else if (h.id == "brush_out") brush_radius = max(1, brush_radius - 1);
        else if (h.id == "step_in") item_step = min(64, item_step + 1);
        else if (h.id == "step_out") item_step = max(1, item_step - 1);
        else if (h.id == "toggle_grid") show_grid = !show_grid;
        else if (h.id == "clear_map") {
            push_undo();
            map.clear(palette[0]);
            selected_item = -1;
        }
        else if (h.id == "map_w_in") {
            push_undo();
            map.resize_keep(map.w + 2, map.h, palette[0]);
        }
        else if (h.id == "map_w_out") {
            push_undo();
            map.resize_keep(map.w - 2, map.h, palette[0]);
        }
        else if (h.id == "map_h_in") {
            push_undo();
            map.resize_keep(map.w, map.h + 2, palette[0]);
        }
        else if (h.id == "map_h_out") {
            push_undo();
            map.resize_keep(map.w, map.h - 2, palette[0]);
        }
        else if (h.id == "path_prev") {
            input_status_msg.clear();
            apply_item_path_index(current_item_path - 1);
        }
        else if (h.id == "path_next") {
            input_status_msg.clear();
            apply_item_path_index(current_item_path + 1);
        }
        else if (h.id == "reload_items") {
            input_status_msg.clear();
            templates = load_templates(items_base);
            current_template = 0;
            current_frame = 0;
            sanitize_item_cache();
        }
        else if (h.id == "tpl_prev") {
            if (!templates.empty()) {
                current_template = (current_template - 1 + (int)templates.size()) % (int)templates.size();
                current_frame = 0;
                update_item_cache(current_template, current_frame);
            }
        }
        else if (h.id == "tpl_next") {
            if (!templates.empty()) {
                current_template = (current_template + 1) % (int)templates.size();
                current_frame = 0;
                update_item_cache(current_template, current_frame);
            }
        }
        else if (h.id == "frame_prev") {
            if (!templates.empty()) {
                int n = templates[current_template].frames.size();
                if (n > 0) {
                    current_frame = (current_frame - 1 + n) % n;
                    update_item_cache(current_template, current_frame);
                }
            }
        }
        else if (h.id == "frame_next") {
            if (!templates.empty()) {
                int n = templates[current_template].frames.size();
                if (n > 0) {
                    current_frame = (current_frame + 1) % n;
                    update_item_cache(current_template, current_frame);
                }
            }
        }
        else if (h.id == "cache_item") {
            use_cached_item(h.value);
        }
        else if (h.id == "cache_use_top") {
            use_cached_item(0);
        }
        else if (h.id == "copy_item") {
            push_undo();
            copy_selected_item();
        }
        else if (h.id == "delete_item") {
            push_undo();
            delete_selected_item();
        }
        else if (h.id == "unselect_item") {
            selected_item = -1;
        }
        else if (h.id == "export_json") {
            string fn = "map_export_" + now_string() + ".json";
            export_json(fn);
            export_json("map_editor_last.json");
            cout << "[Export] " << fn << " and map_editor_last.json" << endl;
        }
        else if (h.id == "import_json") {
            push_undo();
            if (import_json("map_editor_last.json")) cout << "[Import] map_editor_last.json" << endl;
            else cout << "[Import Failed] map_editor_last.json" << endl;
        }
        else if (h.id == "undo") undo();
        else if (h.id == "redo") redo();

        clamp_camera();
        mark_canvas_full_dirty();
        mark_panel_dirty();
    }

    void begin_canvas_left(int sx, int sy) {
        if (tool == TOOL_BRUSH || tool == TOOL_ERASER) {
            push_undo();
            draw_stroke(sx, sy, tool == TOOL_ERASER);
            drag_mode = DRAG_DRAW;
            return;
        }
        if (tool == TOOL_FILL) {
            push_undo();
            flood_fill(sx, sy);
            mark_canvas_full_dirty();
            mark_panel_dirty();
            return;
        }
        if (tool == TOOL_PICK) {
            pick_color(sx, sy);
            mark_panel_dirty();
            return;
        }
        if (tool == TOOL_ITEM_PLACE) {
            push_undo();
            place_item(sx, sy);
            mark_canvas_full_dirty();
            mark_panel_dirty();
            return;
        }
        if (tool == TOOL_ITEM_EDIT) {
            int cx, cy;
            if (!mouse_to_cell(sx, sy, cx, cy)) return;
            int idx = find_item_at(cx, cy);
            selected_item = idx;
            if (idx >= 0) {
                push_undo();
                move_offset_x = cx - map.items[idx].x;
                move_offset_y = cy - map.items[idx].y;
                drag_mode = DRAG_ITEM_MOVE;
            }
            mark_canvas_full_dirty();
            mark_panel_dirty();
        }
    }

    void drag_canvas(int sx, int sy) {
        if (drag_mode == DRAG_DRAW) {
            draw_stroke(sx, sy, tool == TOOL_ERASER);
            return;
        }
        if (drag_mode == DRAG_ITEM_MOVE && selected_item >= 0 && selected_item < (int)map.items.size()) {
            int cx, cy;
            if (!mouse_to_cell(sx, sy, cx, cy)) return;
            int nx = cx - move_offset_x;
            int ny = cy - move_offset_y;
            nx = max(0, min(map.h - 1, nx));
            ny = max(0, min(map.w - 1, ny));
            map.items[selected_item].x = nx;
            map.items[selected_item].y = ny;
            mark_canvas_full_dirty();
            mark_panel_dirty();
            return;
        }
    }

    void update_hover_dirty() {
        int nx = -1, ny = -1;
        if (mouse_x >= 0 && mouse_x < canvas_w && mouse_y >= 0 && mouse_y < canvas_h) {
            nx = camera_x + mouse_y / max(1, cell_px);
            ny = camera_y + mouse_x / max(1, cell_px);
            if (!map.in_bounds(nx, ny)) {
                nx = -1;
                ny = -1;
            }
        }
        if (nx == hover_cx && ny == hover_cy) return;
        if (hover_cx >= 0 && hover_cy >= 0) render_cell_to_canvas(hover_cx, hover_cy);
        hover_cx = nx;
        hover_cy = ny;
        if (hover_cx >= 0 && hover_cy >= 0) render_cell_to_canvas(hover_cx, hover_cy);
    }

public:
    explicit MapEditor(const string& items_arg = "") {
        init_palette();

        dpy = XOpenDisplay(nullptr);
        if (!dpy) {
            fprintf(stderr, "Cannot open X11 display.\n");
            exit(1);
        }

        scr = DefaultScreen(dpy);
        win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 30, 30, win_w, win_h, 1,
                                  BlackPixel(dpy, scr), WhitePixel(dpy, scr));
        XStoreName(dpy, win, "Elegant Map Editor - Mouse First");
        XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                                   PointerMotionMask | StructureNotifyMask);
        XMapWindow(dpy, win);
        gc = DefaultGC(dpy, scr);

        font = XLoadQueryFont(dpy, "9x15");
        if (!font) font = XLoadQueryFont(dpy, "fixed");
        if (font) XSetFont(dpy, gc, font->fid);

        string env_path;
        const char* env_items = getenv("MAP_EDITOR_ITEMS");
        if (env_items && *env_items) env_path = env_items;
        string initial_path = !items_arg.empty() ? items_arg : (!env_path.empty() ? env_path : "./items");
        item_paths = discover_item_paths(initial_path);
        apply_item_path_index(0);
        if (!templates.empty()) update_item_cache(0, 0);

        create_canvas_image();

        cout << "=========================================================\n";
        cout << "地图编辑器（鼠标主导版）已启动\n";
        cout << "- 右侧面板全部可点击操作\n";
        cout << "- 颜色选择器：调色板 + RGB 三滑条\n";
        cout << "- Item：导入模板/放置/选择拖动/复制/删除\n";
        cout << "- Item地址支持：启动参数、MAP_EDITOR_ITEMS、面板Path切换\n";
        cout << "- 导入导出：map_editor_last.json + 时间戳版本\n";
        cout << "=========================================================\n";
    }

    void run() {
        auto handle_event = [&](XEvent& ev) {
            if (ev.type == ConfigureNotify) {
                win_w = max(980, ev.xconfigure.width);
                win_h = max(640, ev.xconfigure.height);
                canvas_w = max(520, win_w - panel_w);
                canvas_h = win_h;
                create_canvas_image();
                clamp_camera();
                hover_cx = hover_cy = -1;
                mark_canvas_full_dirty();
                mark_panel_dirty();
            } else if (ev.type == KeyPress) {
                if (input_active) {
                    handle_input_key(&ev.xkey);
                    mark_panel_dirty();
                    return;
                }
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_Escape) running = false;
            } else if (ev.type == MotionNotify) {
                mouse_x = ev.xmotion.x;
                mouse_y = ev.xmotion.y;

                if (drag_mode == DRAG_PAN) {
                    int dx = ev.xmotion.x - pan_last_x;
                    int dy = ev.xmotion.y - pan_last_y;
                    camera_y -= dx / max(1, cell_px);
                    camera_x -= dy / max(1, cell_px);
                    clamp_camera();
                    pan_last_x = ev.xmotion.x;
                    pan_last_y = ev.xmotion.y;
                    mark_canvas_full_dirty();
                    mark_panel_dirty();
                } else if (drag_mode == DRAG_R || drag_mode == DRAG_G || drag_mode == DRAG_B) {
                    if (drag_mode == DRAG_R) slider_update("slider_r", ev.xmotion.x);
                    if (drag_mode == DRAG_G) slider_update("slider_g", ev.xmotion.x);
                    if (drag_mode == DRAG_B) slider_update("slider_b", ev.xmotion.x);
                    mark_panel_dirty();
                } else {
                    drag_canvas(ev.xmotion.x, ev.xmotion.y);
                    update_hover_dirty();
                }
            } else if (ev.type == ButtonPress) {
                mouse_x = ev.xbutton.x;
                mouse_y = ev.xbutton.y;

                if (ev.xbutton.button == Button4) {
                    cell_px = min(40, cell_px + 1);
                    clamp_camera();
                    hover_cx = hover_cy = -1;
                    mark_canvas_full_dirty();
                    mark_panel_dirty();
                    return;
                }
                if (ev.xbutton.button == Button5) {
                    cell_px = max(4, cell_px - 1);
                    clamp_camera();
                    hover_cx = hover_cy = -1;
                    mark_canvas_full_dirty();
                    mark_panel_dirty();
                    return;
                }

                if (ev.xbutton.button == Button2) {
                    middle_down = true;
                    drag_mode = DRAG_PAN;
                    pan_last_x = ev.xbutton.x;
                    pan_last_y = ev.xbutton.y;
                    return;
                }

                if (ev.xbutton.button == Button3) {
                    if (ev.xbutton.x < canvas_w && ev.xbutton.y < canvas_h) {
                        pick_color(ev.xbutton.x, ev.xbutton.y);
                        mark_panel_dirty();
                    }
                    return;
                }

                if (ev.xbutton.button == Button1) {
                    left_down = true;
                    int hi = hit_index(ev.xbutton.x, ev.xbutton.y);
                    if (hi >= 0) {
                        if (hits[hi].id == "slider_r") {
                            drag_mode = DRAG_R;
                            slider_update("slider_r", ev.xbutton.x);
                            mark_panel_dirty();
                        } else if (hits[hi].id == "slider_g") {
                            drag_mode = DRAG_G;
                            slider_update("slider_g", ev.xbutton.x);
                            mark_panel_dirty();
                        } else if (hits[hi].id == "slider_b") {
                            drag_mode = DRAG_B;
                            slider_update("slider_b", ev.xbutton.x);
                            mark_panel_dirty();
                        } else if (hits[hi].id == "item_path") {
                            input_active = true;
                            input_target = "item_path";
                            input_buffer = items_base;
                            input_box_x = hits[hi].x;
                            input_box_y = hits[hi].y;
                            input_box_w = hits[hi].w;
                            input_box_h = hits[hi].h;
                            input_cursor_timer = 0;
                            input_cursor_visible = true;
                            mark_panel_dirty();
                        } else {
                            panel_action(hits[hi]);
                        }
                    } else if (ev.xbutton.x < canvas_w && ev.xbutton.y < canvas_h) {
                        if (input_active) cancel_input();
                        begin_canvas_left(ev.xbutton.x, ev.xbutton.y);
                    }
                }
            } else if (ev.type == ButtonRelease) {
                if (ev.xbutton.button == Button1) {
                    left_down = false;
                    if (drag_mode == DRAG_DRAW || drag_mode == DRAG_ITEM_MOVE || drag_mode == DRAG_R || drag_mode == DRAG_G || drag_mode == DRAG_B) {
                        drag_mode = DRAG_NONE;
                    }
                }
                if (ev.xbutton.button == Button2) {
                    middle_down = false;
                    if (drag_mode == DRAG_PAN) drag_mode = DRAG_NONE;
                }
            } else if (ev.type == Expose) {
                mark_canvas_full_dirty();
                mark_panel_dirty();
            }
        };

        while (running) {
            bool had_event = false;
            if (!needs_redraw && !XPending(dpy)) {
                XEvent ev;
                XNextEvent(dpy, &ev);
                had_event = true;
                handle_event(ev);
            }

            while (XPending(dpy)) {
                XEvent ev;
                XNextEvent(dpy, &ev);
                had_event = true;
                handle_event(ev);
            }

            if (needs_redraw) {
                if (input_active) {
                    input_cursor_timer++;
                    input_cursor_visible = (input_cursor_timer / 30) % 2 == 0;
                }
                flush_canvas_dirty();
                if (panel_dirty) {
                    draw_panel();
                    panel_dirty = false;
                }
                draw_canvas_cursor_hint();
                XFlush(dpy);
                needs_redraw = false;
            }

            if (!had_event) usleep(1000);
        }

        export_json("map_editor_last.json");
    }

    ~MapEditor() {
        if (canvas_img) {
            XDestroyImage(canvas_img);
            canvas_img = nullptr;
        }
        if (font) XFreeFont(dpy, font);
        if (dpy) XCloseDisplay(dpy);
    }
};

int main(int argc, char** argv) {
    string items_path_arg;
    if (argc >= 2) items_path_arg = argv[1];
    MapEditor editor(items_path_arg);
    editor.run();
    return 0;
}
