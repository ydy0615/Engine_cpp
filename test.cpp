#include<bits/stdc++.h>
#include<unistd.h>
#include<png.h>
#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<X11/keysym.h>
#include"json.hpp"
using namespace std;
using namespace nlohmann;

class Color{
public:
    unsigned char r=0,g=0,b=0,a=0;
    void empty(){
        r=g=b=0;
        a=0;
    }
    bool is_empty() const {
        return a==0;
    }
};

class Image{
public:
    int w=0,h=0;
    vector<vector<Color>> mat;
    void init(int W,int H){
        w=W;
        h=H;
        mat.assign(H,vector<Color>(W));
    }
};

bool readPNG(const string& filename,Image& img){
    FILE* fp=fopen(filename.c_str(),"rb");
    if(!fp) return false;
    png_byte header[8];
    if(fread(header,1,8,fp)!=8 || png_sig_cmp(header,0,8)){
        fclose(fp);
        return false; 
    }
    png_structp png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);
    png_infop info_ptr=png_create_info_struct(png_ptr);
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr,&info_ptr,nullptr);
        fclose(fp);return false;
    }
    png_init_io(png_ptr,fp);
    png_set_sig_bytes(png_ptr,8);
    png_read_info(png_ptr,info_ptr);
    int width=png_get_image_width(png_ptr,info_ptr);
    int height=png_get_image_height(png_ptr,info_ptr);
    int color_type=png_get_color_type(png_ptr,info_ptr);
    int bit_depth=png_get_bit_depth(png_ptr,info_ptr);
    if(bit_depth==16) png_set_strip_16(png_ptr);
    if(color_type==PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if(color_type==PNG_COLOR_TYPE_GRAY && bit_depth<8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if(png_get_valid(png_ptr,info_ptr,PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if(color_type==PNG_COLOR_TYPE_GRAY || color_type==PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png_ptr);
    if(!(color_type & PNG_COLOR_MASK_ALPHA)) png_set_filler(png_ptr,0xFF,PNG_FILLER_AFTER);
    
    png_read_update_info(png_ptr,info_ptr);
    int channels=png_get_channels(png_ptr,info_ptr);
    img.init(width,height);
    png_bytep* row_pointers=new png_bytep[height];
    for(int i=0;i<height;i++) row_pointers[i]=new png_byte[png_get_rowbytes(png_ptr,info_ptr)];
    png_read_image(png_ptr,row_pointers);
    
    for(int y=0;y<height;y++){
        for(int x=0;x<width;x++){
            png_bytep pixel=&row_pointers[y][x*channels];
            img.mat[y][x].r=pixel[0];
            img.mat[y][x].g=pixel[1];
            img.mat[y][x].b=pixel[2];
            img.mat[y][x].a=(channels>=4)?pixel[3]:255;
        }
        delete[] row_pointers[y];
    }
    delete[] row_pointers;
    png_destroy_read_struct(&png_ptr,&info_ptr,nullptr);
    fclose(fp);
    return true;
}

int clamp_int(int value,int low,int high){
    if(low>high) return low;
    return max(low,min(value,high));
}

void blit_image(Image& dest,const Image& src,int top,int left){
    if(dest.mat.empty() || src.mat.empty()) return;
    int start_row=max(0,-top);
    int start_col=max(0,-left);
    int end_row=min(src.h,dest.h-top);
    int end_col=min(src.w,dest.w-left);
    for(int row=start_row;row<end_row;row++){
        for(int col=start_col;col<end_col;col++){
            const Color& src_color=src.mat[row][col];
            if(src_color.is_empty()) continue;
            Color& dest_color=dest.mat[top+row][left+col];
            if(src_color.a==255 || dest_color.is_empty()){
                dest_color=src_color;
                continue;
            }
            int src_alpha=src_color.a;
            int inv_alpha=255-src_alpha;
            dest_color.r=(unsigned char)((src_color.r*src_alpha + dest_color.r*inv_alpha)/255);
            dest_color.g=(unsigned char)((src_color.g*src_alpha + dest_color.g*inv_alpha)/255);
            dest_color.b=(unsigned char)((src_color.b*src_alpha + dest_color.b*inv_alpha)/255);
            dest_color.a=(unsigned char)min(255,src_alpha + dest_color.a*inv_alpha/255);
        }
    }
}

class UI{
public:
    vector<Image> status;
    int x,y,chosen=0,tot=0;
    string Path;
    bool switch_to(int i){
        if(i>tot || i<0) return false;
        chosen=i;
        return true;
    }
    bool switch_next(){
        if(tot<=0) return false;
        chosen++;
        chosen=chosen%(tot+1);
        if(chosen<1) chosen=1;
        return true;
    }
    bool read(string path,int num){
        status.clear();
        status.push_back(Image());
        chosen=0;
        for (int i=1;i<=num;i++){
            Image img;string filename=path+"/"+to_string(i)+".png";
            if(readPNG(filename,img)) status.push_back(img);
            else return false;
        }
        Path=path;
        tot=num;
        return true;
    }
    void add_UI_to(Image& Imagefile){
        if(chosen==0 || chosen >= status.size()) return;
        blit_image(Imagefile,status[chosen],x,y);
    }
};

class Item {
public:
    vector<Image> status;
    vector<pair<int,int>> positions;
    vector<int> chosen;
    vector<bool> visible;
    int tot=0,sum=0,access_able=1;
    string Path;
    bool read(string path,int num,int tot_num,vector<pair<int,int> >& poses){
        status.clear();
        positions.clear();
        chosen.clear();
        visible.clear();
        if(tot_num==0 || num==0 || poses.size()<tot_num) return false;
        status.push_back(Image());
        chosen.push_back(0);
        visible.push_back(1);
        positions.push_back({0,0});
        for(int i=1;i<=num;i++){
            Image img;
            string filename=path+"/"+to_string(i)+".png";
            if(readPNG(filename, img)){
                status.push_back(img);
            }
            else{
                return false;
            }
        }
        for(int i=1;i<=tot_num;i++){
            chosen.push_back(0);
            visible.push_back(1);
            positions.push_back(poses[i-1]);
        }
        tot=num;
        sum=tot_num;
        Path=path;
        return true;
    }
    void apply_to(Image& process,int x,int y){
        if(tot==0 || sum==0) return ;
        for(int i=1;i<=sum;i++){
            if(!visible[i] || !chosen[i]) continue;
            if(chosen[i]>=status.size()) continue;
            blit_image(process,status[chosen[i]],positions[i].first-x,positions[i].second-y);
        }
    }
    void add_position(int x,int y){
        sum++;
        positions.push_back({x,y});
        chosen.push_back(0);
        visible.push_back(true);
    }
    bool set_visible(int idx,bool v){
        if(idx>=0 && idx<=sum){
            visible[idx]=v;
            return true;
        }
        return false;
    }
    bool set_chosen(int idx,int c){
        if(idx>=0 && idx<=sum){
            chosen[idx]=c;
            return true;
        }
        return false;
    }
    bool set_access(int c){
        access_able=c;
        return true;
    }
};

class Map{
public:
    Image Whole;
    unordered_map<string,Item> Items;
    vector<string> Item_names;
    string Path;
    bool init(string path){
        if(!readPNG(path,Whole)){
            return false;
        }
        Item_names.clear();
        Items.clear();
        Path=path;
        return true;
    }
    bool add_Item(string name,Item& item){
        Items[name]=item;
        Item_names.push_back(name);
        return true;
    }
    Image get_part(int x,int y,int w,int h){
        Image ans;
        ans.init(w,h);
        if(Whole.h <= 0 || Whole.w <= 0) return ans;
        
        int limit_x = min(h, Whole.h - x);
        int limit_y = min(w, Whole.w - y);
        
        for(int i=0; i<limit_x; i++){
            for(int j=0; j<limit_y; j++){
                if (x+i >= 0 && x+i < Whole.h && y+j >= 0 && y+j < Whole.w) {
                    ans.mat[i][j] = Whole.mat[x+i][y+j];
                }
            }
        }
        for(string names:Item_names){
            Items[names].apply_to(ans,x,y);
        }
        return ans;
    }
    bool write_json(string file_path){
        try{
            json j;
            j["Path"]=Path;
            j["Item_names"]=Item_names;
            json j_items;
            for(const string& name : Item_names){
                if(Items.find(name)==Items.end()) continue;
                Item& it=Items[name];
                json j_it;
                j_it["Path"]=it.Path;
                j_it["access_able"]=it.access_able;
                j_it["tot"]=it.tot;
                j_it["sum"]=it.sum;
                json j_pos=json::array();
                for(auto& p : it.positions){
                    j_pos.push_back({p.first,p.second});
                }
                j_it["positions"]=j_pos;
                j_it["visible"]=it.visible;
                j_it["chosen"]=it.chosen;
                j_items[name]=j_it;
            }
            j["Items"]=j_items;
            ofstream f(file_path);
            if(!f.is_open()) return 0;
            f<<j.dump(4);
            f.close();
            return 1;
        }
        catch(...){return 0;}
    }
    bool read_json(string file_path){
        try{
            ifstream f(file_path);
            if(!f.is_open()) return 0;
            json j;
            f>>j;
            f.close();
            if(j.contains("Path")){
                Path=j["Path"].get<string>();
                init(Path);
            }
            else{
                return 0;
            }
            if(j.contains("Item_names")){
                Item_names=j["Item_names"].get<vector<string>>();
            }
            if(j.contains("Items")){
                json j_items=j["Items"];
                for(const string& name : Item_names){
                    if(!j_items.contains(name)) continue;
                    json j_it=j_items[name];
                    Item it;
                    it.Path=j_it["Path"].get<string>();
                    it.access_able=j_it["access_able"].get<int>();
                    it.tot=j_it["tot"].get<int>();
                    it.sum=j_it["sum"].get<int>();
                    it.positions.clear();
                    if(j_it.contains("positions")){
                        for(auto& p : j_it["positions"]){
                            it.positions.push_back({p[0].get<int>(), p[1].get<int>()});
                        }
                    }
                    if(j_it.contains("visible")){
                        it.visible=j_it["visible"].get<vector<bool>>();
                    }
                    if(j_it.contains("chosen")){
                        it.chosen=j_it["chosen"].get<vector<int>>();
                    }
                    it.status.clear();
                    it.status.push_back(Image());
                    for(int i=1;i<=it.tot;i++){
                        Image img;
                        string filename=it.Path+"/"+to_string(i) + ".png";
                        if(readPNG(filename,img)){
                            it.status.push_back(img);
                        }
                        else{
                            return 0;
                        }
                    }
                    Items[name]=it;
                }
            }
            return 1;
        }
        catch(...){return 0;}
    }
};

class X11Renderer{
    Display* dpy;
    Window win;
    int scr;
    GC gc;
    XImage* ximage = nullptr;
    Atom wm_delete_window;
    bool closed=false;
    int width, height, scale;
public:
    X11Renderer(int w, int h, int s) : width(w), height(h), scale(s) {
        dpy = XOpenDisplay(nullptr);
        if (!dpy) { fprintf(stderr, "Cannot open display\n"); exit(1); }
        scr = DefaultScreen(dpy);
        win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 10, 10, w * scale, h * scale, 1, 
                                 BlackPixel(dpy, scr), WhitePixel(dpy, scr));
        
        XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
        XMapWindow(dpy, win);
        gc = DefaultGC(dpy, scr);
        wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dpy, win, &wm_delete_window, 1);
        XStoreName(dpy, win, "Single File Engine");
        
        char* data = (char*)malloc(w * scale * h * scale * 4);
        ximage = XCreateImage(dpy, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr), ZPixmap, 0, data, w * scale, h * scale, 32, 0);
    }

    void draw(const Image& img) {
        if (img.mat.empty()) return;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                Color c = (y < img.h && x < img.w) ? img.mat[y][x] : Color{0,0,0,255};
                if(c.a==0) c=Color{0,0,0,255};
                unsigned long pixel = (c.r << 16) | (c.g << 8) | c.b;
                
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        XPutPixel(ximage, x * scale + sx, y * scale + sy, pixel);
                    }
                }
            }
        }
        XPutImage(dpy, win, gc, ximage, 0, 0, 0, 0, width * scale, height * scale);
        XFlush(dpy);
    }

    void poll_events() {
        while (XPending(dpy)) { 
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wm_delete_window) {
                closed = true;
            }
            if (ev.type == DestroyNotify) {
                closed = true;
            }
        }
    }

    bool is_open() const {
        return !closed;
    }

    bool key_down(char key) const {
        KeySym ks=NoSymbol;
        if(key=='w') ks=XK_w;
        else if(key=='a') ks=XK_a;
        else if(key=='s') ks=XK_s;
        else if(key=='d') ks=XK_d;
        else if(key=='q') ks=XK_q;
        else if(key=='e') ks=XK_e;
        if(ks==NoSymbol) return false;
        KeyCode key_code=XKeysymToKeycode(dpy,ks);
        if(key_code==0) return false;
        char key_map[32];
        XQueryKeymap(dpy,key_map);
        return key_map[key_code/8] & (1<<(key_code%8));
    }

    void set_title(const string& title) {
        XStoreName(dpy, win, title.c_str());
    }
    ~X11Renderer(){
        if(ximage) XDestroyImage(ximage);
        if(dpy) XCloseDisplay(dpy);
    }
};

class UI_system{
public:
	unordered_map<string,UI> UIs;
	vector<string> name_UIs;

	void init_UIs(){
		UIs.clear();
		name_UIs.clear();
	}

	void apply_UIs(Image& screen){
		for(string name:name_UIs){
			UIs[name].add_UI_to(screen);
		}
	}

	void add_UI(string name,string path,int h,int w,int num){
		UI temp;
		temp.read(path,num);
		temp.x=h;temp.y=w;
		temp.switch_to(1);
		UIs[name]=temp;
		name_UIs.push_back(name);
	}

	void switch_UI(string name,int status){
		UIs[name].switch_to(status);
	}
};

class Effect {
public:
	vector<Image> status;
	int tot;
	string Path;
	int frame_delay;
	bool loop;
	
    Effect() : tot(0), frame_delay(8), loop(true) {}
	
	bool read(const string& path, int num) {
        status.clear();
        status.push_back(Image());
        for (int i = 1; i <= num; i++) {
            string filename = path + "/" + to_string(i) + ".png";
            Image img;
            if (readPNG(filename, img)) status.push_back(img);
            else return false;
        }
        Path = path;
        tot = num;
        return true;
    }
	
	void set_frame_delay(int fd) { frame_delay = max(1, fd); }
	void set_loop(bool l) { loop = l; }
	
	const Image* get_frame(int current_frame) const {
	        if (current_frame > 0 && current_frame <= tot) return &status[current_frame];
        return nullptr;
    }
};

struct PointEffect {
	string name;
	int pos_x, pos_y;
	int lifetime;
	int age;
	int current_frame;
	int timer;
	int frame_delay;
	bool loop;
	bool active;
	
	PointEffect() : pos_x(0), pos_y(0), lifetime(-1), age(0), current_frame(1), timer(0), frame_delay(8), loop(true), active(true) {}
	
	PointEffect(const string& n, int x, int y, int life, int delay, bool should_loop) 
		: name(n), pos_x(x), pos_y(y), lifetime(life), age(0), current_frame(1), timer(0), frame_delay(max(1, delay)), loop(should_loop), active(true) {}
	
	bool is_expired() const {
	        return lifetime >= 0 && age >= lifetime;
    }
};

class EffectSystem {
public:
	unordered_map<string, Effect> effect_types;
	vector<string> type_names;
	vector<PointEffect> active_effects;
	
bool add_effect_type(const string& name, const string& path, int num) {
		Effect temp;
		if (!temp.read(path, num)) return false;
		effect_types[name] = temp;
		type_names.push_back(name);
		return true;
	}
	
int spawn_effect(const string& name, int x, int y, int lifetime = -1) {
        auto it=effect_types.find(name);
        if (it == effect_types.end()) return -1;
        PointEffect pe(name, x, y, lifetime, it->second.frame_delay, it->second.loop);
		active_effects.push_back(pe);
		return active_effects.size() - 1;
	}
	
void update() {
		for (auto& pe : active_effects) {
			if (!pe.active) continue;
            auto it = effect_types.find(pe.name);
            if (it == effect_types.end()) {
                pe.active = false;
                continue;
            }
            const Effect& effect = it->second;
			pe.age++;
            pe.timer++;
            if (effect.tot > 0 && pe.timer >= pe.frame_delay) {
                pe.timer = 0;
                pe.current_frame++;
                if (pe.current_frame > effect.tot) {
                    if (pe.loop) pe.current_frame = 1;
                    else pe.active = false;
                }
			}
			if (pe.is_expired()) pe.active = false;
		}
	}
	
void clear_expired() {
		vector<PointEffect> remaining;
		for (auto& pe : active_effects) {
			if (pe.active) remaining.push_back(pe);
		}
		active_effects.swap(remaining);
	}
	
void remove_effect(int idx) {
		if (idx >= 0 && idx < (int)active_effects.size()) {
			active_effects[idx].active = false;
		}
	}
	
void clear_all() {
		active_effects.clear();
	}
	
void apply_effects(Image& screen, int view_x, int view_y) {
		for (auto& pe : active_effects) {
			if (!pe.active) continue;
			auto it = effect_types.find(pe.name);
			if (it == effect_types.end()) continue;
			
            const Effect& eff = it->second;
            const Image* frame = eff.get_frame(pe.current_frame);
			if (!frame) continue;
            blit_image(screen,*frame,pe.pos_y-view_y,pe.pos_x-view_x);
		}
	}
};

struct AABB {
    int x, y, w, h;
    AABB() : x(0), y(0), w(0), h(0) {}
    AABB(int X, int Y, int W, int H) : x(X), y(Y), w(W), h(H) {}
    
    bool intersects(const AABB& other) const {
        return !(x + w <= other.x || other.x + other.w <= x ||
                 y + h <= other.y || other.y + other.h <= y);
    }
    
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

enum class ColliderType { Dynamic, Static, Trigger };

struct Collider {
    string name;
    AABB bounds;
    ColliderType type;
    bool enabled;
    int layer;
    
    Collider() : type(ColliderType::Static), enabled(true), layer(0) {}
    Collider(const string& n, int x, int y, int w, int h, ColliderType t = ColliderType::Static)
        : name(n), bounds(x, y, w, h), type(t), enabled(true), layer(0) {}
};

class PhysicsWorld {
public:
    vector<Collider> colliders;
    
    PhysicsWorld() {}
    
    int add_collider(const Collider& col) {
        colliders.push_back(col);
        return colliders.size() - 1;
    }
    
    void remove_collider(int idx) {
        if (idx >= 0 && idx < (int)colliders.size()) {
            colliders[idx].enabled = false;
        }
    }
    
    void clear() {
        colliders.clear();
    }
    
    vector<int> query_point(int x, int y) const {
        vector<int> result;
        AABB pt(x, y, 1, 1);
        for (int i = 0; i < (int)colliders.size(); i++) {
            if (colliders[i].enabled && colliders[i].bounds.intersects(pt)) {
                result.push_back(i);
            }
        }
        return result;
    }
    
    vector<int> query_aabb(const AABB& box,int ignore_idx=-1) const {
        vector<int> result;
        for (int i = 0; i < (int)colliders.size(); i++) {
            if (i == ignore_idx) continue;
            if (colliders[i].enabled && colliders[i].bounds.intersects(box)) {
                result.push_back(i);
            }
        }
        return result;
    }

    bool test_collision(const AABB& box,int ignore_idx=-1) const {
        return !query_aabb(box,ignore_idx).empty();
    }

    bool set_collider_bounds(int idx,const AABB& bounds) {
        if (idx < 0 || idx >= (int)colliders.size()) return false;
        colliders[idx].bounds = bounds;
        return true;
    }
    
    void update() {}
    
    bool check_collision(int idx) {
        if (idx < 0 || idx >= (int)colliders.size()) return false;
        if (!colliders[idx].enabled) return false;
        return test_collision(colliders[idx].bounds, idx);
    }
};

class ResourceManager {
public:
    unordered_map<string, Image> image_cache;
    unordered_map<string, vector<Image>> animation_cache;
    int cache_limit;
    
    ResourceManager(int limit = 256) : cache_limit(limit) {}
    
    bool load_image(const string& name, const string& path) {
        if (image_cache.size() >= cache_limit) return false;
        Image img;
        if (!readPNG(path, img)) return false;
        image_cache[name] = img;
        return true;
    }
    
    bool load_animation(const string& name, const string& path, int frame_count) {
        if (animation_cache.size() >= cache_limit) return false;
        vector<Image> frames;
        frames.push_back(Image());
        for (int i = 1; i <= frame_count; i++) {
            Image img;
            string filename = path + "/" + to_string(i) + ".png";
            if (!readPNG(filename, img)) return false;
            frames.push_back(img);
        }
        animation_cache[name] = frames;
        return true;
    }
    
    Image* get_image(const string& name) {
        auto it = image_cache.find(name);
        if (it != image_cache.end()) return &it->second;
        return nullptr;
    }
    
    vector<Image>* get_animation(const string& name) {
        auto it = animation_cache.find(name);
        if (it != animation_cache.end()) return &it->second;
        return nullptr;
    }
    
    void clear_cache() {
        image_cache.clear();
        animation_cache.clear();
    }
    
    int loaded_count() const {
        return image_cache.size() + animation_cache.size();
    }
};

struct KeyState {
    bool pressed;
    bool just_pressed;
    bool just_released;
    
    KeyState() : pressed(false), just_pressed(false), just_released(false) {}
};

class InputSystem {
public:
    unordered_map<char, KeyState> keys;
    
    void update() {
        for (auto& kv : keys) {
            kv.second.just_pressed = false;
            kv.second.just_released = false;
        }
    }
    
    void set_key(char key, bool down) {
        auto it = keys.find(key);
        if (it == keys.end()) {
            keys[key] = KeyState();
            it = keys.find(key);
        }
        if (!it->second.pressed && down) {
            it->second.just_pressed = true;
        }
        if (it->second.pressed && !down) {
            it->second.just_released = true;
        }
        it->second.pressed = down;
    }
    
    bool is_down(char key) {
        auto it = keys.find(key);
        return it != keys.end() && it->second.pressed;
    }
    
    bool just_pressed(char key) {
        auto it = keys.find(key);
        return it != keys.end() && it->second.just_pressed;
    }
    
    bool just_released(char key) {
        auto it = keys.find(key);
        return it != keys.end() && it->second.just_released;
    }
    
    void clear() {
        keys.clear();
    }
};

class Engine {
public:
    ResourceManager resources;
    InputSystem input;
    EffectSystem effects;
    PhysicsWorld physics;
    Map* current_map;
    int view_x, view_y;
    int view_w, view_h;
    bool running;
    double fps;
    int frame_count;
    chrono::steady_clock::time_point last_fps_time;
    
    Engine() : current_map(nullptr), view_x(0), view_y(0), view_w(256), view_h(192), running(false), fps(60.0), frame_count(0) {
        last_fps_time = chrono::steady_clock::now();
    }
    
    void init(int width, int height) {
        view_w = width;
        view_h = height;
        running = true;
    }
    
    void update_fps() {
        frame_count++;
        auto now = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::seconds>(now - last_fps_time).count();
        if (elapsed >= 1) {
            fps = (double)frame_count / elapsed;
            frame_count = 0;
            last_fps_time = now;
        }
    }
    
    void set_map(Map* mp) {
        current_map = mp;
    }
    
    void shutdown() {
        running = false;
    }
};

struct WorldProp {
    string sprite_name;
    int x;
    int y;
    int collider_id;

    WorldProp(const string& name,int pos_x,int pos_y)
        : sprite_name(name), x(pos_x), y(pos_y), collider_id(-1) {}
};

struct Player {
    int x=0;
    int y=0;
    int sprite_w=16;
    int sprite_h=16;
    int move_speed=1;
    int collider_id=-1;
    int anim_frame=1;
    int anim_timer=0;
    int anim_delay=6;

    AABB collider_bounds(int next_x,int next_y) const {
        int collider_w=max(8,sprite_w/2);
        int collider_h=max(6,sprite_h/3);
        int offset_x=(sprite_w-collider_w)/2;
        int offset_y=max(0,sprite_h-collider_h-1);
        return AABB(next_x+offset_x,next_y+offset_y,collider_w,collider_h);
    }

    void sync_collider(PhysicsWorld& physics) const {
        if(collider_id<0) return;
        physics.set_collider_bounds(collider_id,collider_bounds(x,y));
    }

    bool try_move(PhysicsWorld& physics,int dx,int dy,int world_w,int world_h) {
        bool moved=false;
        if(dx!=0){
            int next_x=clamp_int(x+dx,0,max(0,world_w-sprite_w));
            if(!physics.test_collision(collider_bounds(next_x,y),collider_id)){
                x=next_x;
                moved=true;
            }
        }
        if(dy!=0){
            int next_y=clamp_int(y+dy,0,max(0,world_h-sprite_h));
            if(!physics.test_collision(collider_bounds(x,next_y),collider_id)){
                y=next_y;
                moved=true;
            }
        }
        sync_collider(physics);
        return moved;
    }

    void update_animation(bool moving,int total_frames) {
        total_frames=max(1,total_frames);
        if(!moving){
            anim_frame=1;
            anim_timer=0;
            return;
        }
        anim_timer++;
        if(anim_timer>=anim_delay){
            anim_timer=0;
            anim_frame++;
            if(anim_frame>total_frames) anim_frame=1;
        }
    }

    const Image* current_frame(ResourceManager& resources) const {
        vector<Image>* animation=resources.get_animation("main_character");
        if(!animation || animation->size()<=1) return nullptr;
        int total_frames=animation->size()-1;
        return &(*animation)[clamp_int(anim_frame,1,total_frames)];
    }
};

int main(){
    Map mp;
    if(!mp.read_json("temp.json")){
        cerr<<"Failed to load map data from temp.json\n";
        return 1;
    }
    if(mp.Whole.w<=0 || mp.Whole.h<=0){
        cerr<<"Map image is empty or missing\n";
        return 1;
    }
    int view_w=256,view_h=192,scale=2;
    Engine engine;
    engine.init(view_w,view_h);
    engine.set_map(&mp);
    if(!engine.resources.load_animation("main_character","./main_character",4)){
        cerr<<"Failed to load main character animation\n";
        return 1;
    }
    if(!engine.resources.load_image("tree","./items/tree/1.png")){
        cerr<<"Failed to load tree sprite\n";
        return 1;
    }
    engine.effects.add_effect_type("afterimage","./items/main_character",4);
    if(engine.effects.effect_types.find("afterimage")!=engine.effects.effect_types.end()){
        engine.effects.effect_types["afterimage"].set_loop(false);
        engine.effects.effect_types["afterimage"].set_frame_delay(4);
    }
    X11Renderer renderer(view_w,view_h,scale);
    Player player;
    if(const Image* frame=player.current_frame(engine.resources)){
        player.sprite_w=frame->w;
        player.sprite_h=frame->h;
    }
    player.x=clamp_int(mp.Whole.w/2-player.sprite_w/2,0,max(0,mp.Whole.w-player.sprite_w));
    player.y=clamp_int(mp.Whole.h/2-player.sprite_h/2,0,max(0,mp.Whole.h-player.sprite_h));
    AABB player_box=player.collider_bounds(player.x,player.y);
    player.collider_id=engine.physics.add_collider(Collider("player",player_box.x,player_box.y,player_box.w,player_box.h,ColliderType::Dynamic));

    vector<WorldProp> props;
    auto add_tree=[&](int world_x,int world_y){
        WorldProp prop("tree",world_x,world_y);
        Image* sprite=engine.resources.get_image("tree");
        int sprite_w=sprite ? sprite->w : 32;
        int sprite_h=sprite ? sprite->h : 32;
        int trunk_w=max(10,sprite_w/3);
        int trunk_h=max(8,sprite_h/4);
        int trunk_x=world_x+(sprite_w-trunk_w)/2;
        int trunk_y=world_y+sprite_h-trunk_h;
        prop.collider_id=engine.physics.add_collider(Collider("tree",trunk_x,trunk_y,trunk_w,trunk_h,ColliderType::Static));
        props.push_back(prop);
    };
    add_tree(clamp_int(mp.Whole.w/4,0,max(0,mp.Whole.w-32)),clamp_int(mp.Whole.h/3,0,max(0,mp.Whole.h-32)));
    add_tree(clamp_int(mp.Whole.w/2,0,max(0,mp.Whole.w-32)),clamp_int(mp.Whole.h/2+32,0,max(0,mp.Whole.h-32)));
    add_tree(clamp_int(mp.Whole.w*3/4,0,max(0,mp.Whole.w-32)),clamp_int(mp.Whole.h/4,0,max(0,mp.Whole.h-32)));

    int camera_left=clamp_int(player.x+player.sprite_w/2-view_w/2,0,max(0,mp.Whole.w-view_w));
    int camera_top=clamp_int(player.y+player.sprite_h/2-view_h/2,0,max(0,mp.Whole.h-view_h));
    auto last_frame_time=chrono::steady_clock::now();
    const auto frame_budget=chrono::milliseconds(16);
    const string tracked_keys="wasdqe";
    
    while(engine.running && renderer.is_open()){
        engine.input.update();
        renderer.poll_events();
        for(char key:tracked_keys){
            engine.input.set_key(key,renderer.key_down(key));
        }
        if(engine.input.just_pressed('q')){
            engine.shutdown();
            break;
        }

        int move_x=0,move_y=0;
        if(engine.input.is_down('a')) move_x-=player.move_speed;
        if(engine.input.is_down('d')) move_x+=player.move_speed;
        if(engine.input.is_down('w')) move_y-=player.move_speed;
        if(engine.input.is_down('s')) move_y+=player.move_speed;
        bool moved=player.try_move(engine.physics,move_x,move_y,mp.Whole.w,mp.Whole.h);
        vector<Image>* player_animation=engine.resources.get_animation("main_character");
        int player_frames=player_animation ? (int)player_animation->size()-1 : 1;
        player.update_animation(moved,player_frames);

        if(engine.input.just_pressed('e')){
            engine.effects.spawn_effect("afterimage",player.x,player.y,18);
        }

        engine.effects.update();
        engine.effects.clear_expired();
        camera_left=clamp_int(player.x+player.sprite_w/2-view_w/2,0,max(0,mp.Whole.w-view_w));
        camera_top=clamp_int(player.y+player.sprite_h/2-view_h/2,0,max(0,mp.Whole.h-view_h));

        Image screen=mp.get_part(camera_top,camera_left,view_w,view_h);
        for(const WorldProp& prop:props){
            Image* sprite=engine.resources.get_image(prop.sprite_name);
            if(sprite) blit_image(screen,*sprite,prop.y-camera_top,prop.x-camera_left);
        }
        engine.effects.apply_effects(screen,camera_left,camera_top);
        if(const Image* frame=player.current_frame(engine.resources)){
            blit_image(screen,*frame,player.y-camera_top,player.x-camera_left);
        }
        renderer.draw(screen);

        engine.update_fps();
        ostringstream title;
        title<<fixed<<setprecision(1)
             <<"Single File Engine | FPS "<<engine.fps
             <<" | Player("<<player.x<<","<<player.y<<")"
             <<" | Camera("<<camera_left<<","<<camera_top<<")"
             <<" | Effects "<<engine.effects.active_effects.size()
             <<" | E spawn, Q quit";
        renderer.set_title(title.str());

        auto frame_elapsed=chrono::steady_clock::now()-last_frame_time;
        if(frame_elapsed<frame_budget){
            auto sleep_time=chrono::duration_cast<chrono::microseconds>(frame_budget-frame_elapsed).count();
            if(sleep_time>0) usleep((useconds_t)sleep_time);
        }
        last_frame_time=chrono::steady_clock::now();
    }
    return 0;
}