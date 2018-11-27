// 3D World - City Header
// by Frank Gennari
// 11/19/18

#pragma once

#include "3DWorld.h"
#include "function_registry.h"
#include "inlines.h"
#include "model3d.h"
#include "shaders.h"
#include "draw_utils.h"
#include "buildings.h" // for building_occlusion_state_t

using std::string;

unsigned const CONN_CITY_IX((1<<16)-1); // uint16_t max

enum {TID_SIDEWLAK=0, TID_STRAIGHT, TID_BEND_90, TID_3WAY,   TID_4WAY,   TID_PARK_LOT,  TID_TRACKS,  NUM_RD_TIDS};
enum {TYPE_PLOT   =0, TYPE_RSEG,    TYPE_ISEC2,  TYPE_ISEC3, TYPE_ISEC4, TYPE_PARK_LOT, TYPE_TRACKS, NUM_RD_TYPES};
enum {TURN_NONE=0, TURN_LEFT, TURN_RIGHT, TURN_UNSPEC};
enum {INT_NONE=0, INT_ROAD, INT_PLOT, INT_PARKING};
enum {RTYPE_ROAD=0, RTYPE_TRACKS};
unsigned const CONN_TYPE_NONE = 0;
colorRGBA const road_colors[NUM_RD_TYPES] = {WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE}; // parking lots are darker than roads

int       const FORCE_MODEL_ID = -1; // -1 disables
unsigned  const NUM_CAR_COLORS = 10;
colorRGBA const car_colors[NUM_CAR_COLORS] = {WHITE, GRAY_BLACK, GRAY, ORANGE, RED, DK_RED, DK_BLUE, DK_GREEN, YELLOW, BROWN};

float const ROAD_HEIGHT          = 0.002;
float const PARK_SPACE_WIDTH     = 1.6;
float const PARK_SPACE_LENGTH    = 1.8;
float const CONN_ROAD_SPEED_MULT = 2.0; // twice the speed limit on connector roads
float const HEADLIGHT_ON_RAND    = 0.1;
float const STREETLIGHT_ON_RAND  = 0.05;
float const TUNNEL_WALL_THICK    = 0.25; // relative to radius
float const TRACKS_WIDTH         = 0.5; // relative to road width
vector3d const CAR_SIZE(0.30, 0.13, 0.08); // {length, width, height} in units of road width

extern double tfticks;
extern float fticks;


inline bool is_isect(unsigned type) {return (type >= TYPE_ISEC2 && type <= TYPE_ISEC4);}
inline int encode_neg_ix(unsigned ix) {return -(int(ix)+1);}
inline unsigned decode_neg_ix(int ix) {assert(ix < 0); return -(ix+1);}

inline float rand_hash(float to_hash) {return fract(12345.6789*to_hash);}
inline float signed_rand_hash(float to_hash) {return 0.5*(rand_hash(to_hash) - 1.0);}

struct car_model_t {

	string fn;
	int body_mat_id, fixed_color_id;
	float xy_rot, dz, lod_mult, scale; // xy_rot in degrees
	vector<unsigned> shadow_mat_ids;

	car_model_t() : body_mat_id(-1), fixed_color_id(-1), xy_rot(0.0), dz(0.0), lod_mult(1.0), scale(1.0) {}
	car_model_t(string const &fn_, int bmid, int fcid, float rot, float dz_, float lm, vector<unsigned> const &smids) :
		fn(fn_), body_mat_id(bmid), fixed_color_id(fcid), xy_rot(rot), dz(dz_), lod_mult(lm), shadow_mat_ids(smids) {}
	bool read(FILE *fp);
};

struct city_params_t {

	unsigned num_cities, num_samples, num_conn_tries, city_size_min, city_size_max, city_border, road_border, slope_width, num_rr_tracks;
	float road_width, road_spacing, conn_road_seg_len, max_road_slope;
	unsigned make_4_way_ints; // 0=all 3-way intersections; 1=allow 4-way; 2=all connector roads must have at least a 4-way on one end; 4=only 4-way (no straight roads)
	// cars
	unsigned num_cars;
	float car_speed, traffic_balance_val, new_city_prob, max_car_scale;
	bool enable_car_path_finding;
	vector<car_model_t> car_model_files;
	// parking lots
	unsigned min_park_spaces, min_park_rows;
	float min_park_density, max_park_density;
	// lighting
	bool car_shadows;
	unsigned max_lights, max_shadow_maps;
	// trees
	unsigned max_trees_per_plot;
	float tree_spacing;
	// detail objects
	unsigned max_benches_per_plot;
	// pedestrians
	unsigned num_peds;
	float ped_speed;

	city_params_t() : num_cities(0), num_samples(100), num_conn_tries(50), city_size_min(0), city_size_max(0), city_border(0), road_border(0), slope_width(0),
		num_rr_tracks(0), road_width(0.0), road_spacing(0.0), conn_road_seg_len(1000.0), max_road_slope(1.0), make_4_way_ints(0), num_cars(0), car_speed(0.0),
		traffic_balance_val(0.5), new_city_prob(1.0), max_car_scale(1.0), enable_car_path_finding(0), min_park_spaces(12), min_park_rows(1), min_park_density(0.0),
		max_park_density(1.0), car_shadows(0), max_lights(1024), max_shadow_maps(0), max_trees_per_plot(0), tree_spacing(1.0), max_benches_per_plot(0), num_peds(0), ped_speed(0.0) {}
	bool enabled() const {return (num_cities > 0 && city_size_min > 0);}
	bool roads_enabled() const {return (road_width > 0.0 && road_spacing > 0.0);}
	float get_road_ar() const {return nearbyint(road_spacing/road_width);} // round to nearest texture multiple
	static bool read_error(string const &str) {cout << "Error reading city config option " << str << "." << endl; return 0;}
	bool read_option(FILE *fp);
	vector3d get_nom_car_size() const {return CAR_SIZE*road_width;}
	vector3d get_max_car_size() const {return max_car_scale*get_nom_car_size();}
}; // city_params_t


struct car_t;

struct road_gen_base_t {
	virtual cube_t get_bcube_for_car(car_t const &car) const = 0;
};


struct car_t {
	cube_t bcube, prev_bcube;
	bool dim, dir, stopped_at_light, entering_city, in_tunnel, dest_valid, destroyed;
	unsigned char cur_road_type, color_id, turn_dir, front_car_turn_dir, model_id;
	unsigned short cur_city, cur_road, cur_seg, dest_city, dest_isec;
	float height, dz, rot_z, turn_val, cur_speed, max_speed, waiting_pos, waiting_start;
	car_t const *car_in_front;

	car_t() : bcube(all_zeros), dim(0), dir(0), stopped_at_light(0), entering_city(0), in_tunnel(0), dest_valid(0), destroyed(0), cur_road_type(TYPE_RSEG),
		color_id(0), turn_dir(TURN_NONE), front_car_turn_dir(TURN_UNSPEC), model_id(0), cur_city(0), cur_road(0), cur_seg(0), dest_city(0), dest_isec(0),
		height(0.0), dz(0.0), rot_z(0.0), turn_val(0.0), cur_speed(0.0), max_speed(0.0), waiting_pos(0.0), waiting_start(0.0), car_in_front(nullptr) {}
	bool is_valid() const {return !bcube.is_all_zeros();}
	point get_center() const {return bcube.get_cube_center();}
	unsigned get_orient() const {return (2*dim + dir);}
	unsigned get_orient_in_isec() const {return (2*dim + (!dir));} // invert dir (incoming, not outgoing)
	float get_max_speed() const {return ((cur_city == CONN_CITY_IX) ? CONN_ROAD_SPEED_MULT : 1.0)*max_speed;}
	float get_length() const {return (bcube.d[ dim][1] - bcube.d[ dim][0]);}
	float get_width () const {return (bcube.d[!dim][1] - bcube.d[!dim][0]);}
	float get_max_lookahead_dist() const;
	bool is_almost_stopped() const {return (cur_speed < 0.1*max_speed);}
	bool is_stopped () const {return (cur_speed == 0.0);}
	bool is_parked  () const {return (max_speed == 0.0);}
	bool in_isect   () const {return is_isect(cur_road_type);}
	bool headlights_on() const {return (!is_parked() && (in_tunnel || is_night(HEADLIGHT_ON_RAND*signed_rand_hash(height + max_speed))));} // no headlights when parked
	unsigned get_isec_type() const {assert(in_isect()); return (cur_road_type - TYPE_ISEC2);}
	void park() {cur_speed = max_speed = 0.0;}
	float get_turn_rot_z(float dist_to_turn) const;
	float get_wait_time_secs  () const {return (float(tfticks) - waiting_start)/TICKS_PER_SECOND;} // Note: only meaningful for cars stopped at lights
	colorRGBA const &get_color() const {assert(color_id < NUM_CAR_COLORS); return car_colors[color_id];}
	void apply_scale(float scale);
	void destroy();
	float get_min_sep_dist_to_car(car_t const &c, bool add_one_car_len=0) const;
	string str() const;
	string label_str() const;
	void move(float speed_mult);
	void maybe_accelerate(float mult=0.02);
	void accelerate(float mult=0.02) {cur_speed = min(get_max_speed(), (cur_speed + mult*fticks*max_speed));}
	void decelerate(float mult=0.05) {cur_speed = max(0.0f, (cur_speed - mult*fticks*max_speed));}
	void decelerate_fast() {decelerate(10.0);} // Note: large decel to avoid stopping in an intersection
	void stop() {cur_speed = 0.0;} // immediate stop
	void move_by(float val) {bcube.d[dim][0] += val; bcube.d[dim][1] += val;}
	bool check_collision(car_t &c, road_gen_base_t const &road_gen);
	bool proc_sphere_coll(point &pos, point const &p_last, float radius, vector3d const &xlate, vector3d *cnorm) const;
	point get_front(float dval=0.5) const;
	bool front_intersects_car(car_t const &c) const;
	void honk_horn_if_close() const;
	void honk_horn_if_close_and_fast() const;
	void on_alternate_turn_dir(rand_gen_t &rgen);
	void register_adj_car(car_t &c);
	unsigned count_cars_in_front(cube_t const &range=cube_t(all_zeros)) const;
	float get_sum_len_space_for_cars_in_front(cube_t const &range) const;
};


struct comp_car_road_then_pos {
	vector3d const &xlate;
	comp_car_road_then_pos(vector3d const &xlate_) : xlate(xlate_) {}
	bool operator()(car_t const &c1, car_t const &c2) const;
};


class car_model_loader_t : public model3ds {
	vector<int> models_valid;
	void ensure_models_loaded() {if (empty()) {load_car_models();}}
public:
	static unsigned num_models();

	bool is_model_valid(unsigned id);
	car_model_t const &get_model(unsigned id) const;
	void load_car_models();
	void draw_car(shader_t &s, vector3d const &pos, cube_t const &car_bcube, vector3d const &dir, colorRGBA const &color,
		point const &xlate, unsigned model_id, bool is_shadow_pass, bool low_detail);
};


class road_mat_mgr_t {

	bool inited;
	unsigned tids[NUM_RD_TIDS], sl_tid;

public:
	road_mat_mgr_t() : inited(0), sl_tid(0) {}
	void ensure_road_textures();
	void set_texture(unsigned type);
	void set_stoplight_texture();
};

template<typename T> static void add_flat_road_quad(T const &r, quad_batch_draw &qbd, colorRGBA const &color, float ar) { // z1 == z2
	float const z(r.z1());
	point const pts[4] = {point(r.x1(), r.y1(), z), point(r.x2(), r.y1(), z), point(r.x2(), r.y2(), z), point(r.x1(), r.y2(), z)};
	qbd.add_quad_pts(pts, color, plus_z, r.get_tex_range(ar));
}

struct rect_t {
	unsigned x1, y1, x2, y2;
	rect_t() : x1(0), y1(0), x2(0), y2(0) {}
	rect_t(unsigned x1_, unsigned y1_, unsigned x2_, unsigned y2_) : x1(x1_), y1(y1_), x2(x2_), y2(y2_) {}
	bool is_valid() const {return (x1 < x2 && y1 < y2);}
	unsigned get_area() const {return (x2 - x1)*(y2 - y1);}
	bool operator== (rect_t const &r) const {return (x1 == r.x1 && y1 == r.y1 && x2 == r.x2 && y2 == r.y2);}
	bool has_overlap(rect_t const &r) const {return (x1 < r.x2 && y1 < r.y2 && r.x1 < x2 && r.y1 < y2);}
};
struct flatten_op_t : public rect_t {
	float z1, z2;
	bool dim;
	unsigned border, skip_six, skip_eix;
	flatten_op_t() : z1(0.0), z2(0.0), dim(0), border(0) {}
	flatten_op_t(unsigned x1_, unsigned y1_, unsigned x2_, unsigned y2_, float z1_, float z2_, bool dim_, unsigned border_) :
		rect_t(x1_, y1_, x2_, y2_), z1(z1_), z2(z2_), dim(dim_), border(border_), skip_six(0), skip_eix(0) {}
};

struct road_t : public cube_t {
	unsigned road_ix;
	//unsigned char type; // road, railroad, etc. {RTYPE_ROAD, RTYPE_TRACKS}
	bool dim; // dim the road runs in
	bool slope; // 0: z1 applies to first (lower) point; 1: z1 applies to second (upper) point

	road_t(cube_t const &c, bool dim_, bool slope_=0, unsigned road_ix_=0) : cube_t(c), road_ix(road_ix_), dim(dim_), slope(slope_) {}
	road_t(point const &s, point const &e, float width, bool dim_, bool slope_=0, unsigned road_ix_=0);
	float get_length   () const {return (d[ dim][1] - d[ dim][0]);}
	float get_width    () const {return (d[!dim][1] - d[!dim][0]);}
	float get_slope_val() const {return get_dz()/get_length();}
	float get_start_z  () const {return (slope ? z2() : z1());}
	float get_end_z    () const {return (slope ? z1() : z2());}
	float get_z_adj    () const {return (ROAD_HEIGHT + 0.5*get_slope_val()*(dim ? DY_VAL : DX_VAL));} // account for a half texel of error for sloped roads
	tex_range_t get_tex_range(float ar) const {return tex_range_t(0.0, 0.0, -ar, (dim ? -1.0 : 1.0), 0, dim);}
	cube_t const &get_bcube() const {return *this;}
	cube_t       &get_bcube()       {return *this;}
	void add_road_quad(quad_batch_draw &qbd, colorRGBA const &color, float ar) const;
};

struct road_seg_t : public road_t {
	unsigned short road_ix, conn_ix[2], conn_type[2]; // {dim=0, dim=1}
	mutable unsigned short car_count; // can be written to during car update logic

	void init_ixs() {conn_ix[0] = conn_ix[1] = 0; conn_type[0] = conn_type[1] = CONN_TYPE_NONE;}
	road_seg_t(road_t const &r, unsigned rix) : road_t(r), road_ix(rix), car_count(0) {init_ixs();}
	road_seg_t(cube_t const &c, unsigned rix, bool dim_, bool slope_=0) : road_t(c, dim_, slope_), road_ix(rix), car_count(0) {init_ixs();}
	void next_frame() {car_count = 0;}
};

struct road_plot_t : public cube_t {
	bool has_parking;
	road_plot_t(cube_t const &c) : cube_t(c), has_parking(0) {}
	tex_range_t get_tex_range(float ar) const {return tex_range_t(0.0, 0.0, ar, ar);}
};

struct parking_lot_t : public cube_t {
	bool dim, dir;
	unsigned short row_sz, num_rows;
	parking_lot_t(cube_t const &c, bool dim_, bool dir_, unsigned row_sz_=0, unsigned num_rows_=0) : cube_t(c), dim(dim_), dir(dir_), row_sz(row_sz_), num_rows(num_rows_) {}
	tex_range_t get_tex_range(float ar) const;
};

namespace stoplight_ns {

	enum {GREEN_LIGHT=0, YELLOW_LIGHT, RED_LIGHT}; // colors, unused (only have stop and go states anyway)
	enum {EGL=0, EGWG, WGL, NGL, NGSG, SGL, NUM_STATE}; // E=car moving east, W=west, N=sorth, S=south, G=straight|right, L=left turn
	enum {CW_WALK=0, CW_WARN, CW_STOP}; // crosswalk state
	float const state_times[NUM_STATE] = {5.0, 6.0, 5.0, 5.0, 6.0, 5.0}; // in seconds
	unsigned const st_r_orient_masks[NUM_STATE] = {2, 3, 1, 8, 12, 4}; // {W=1, E=2, S=4, N=8}, for straight and right turns
	unsigned const left_orient_masks[NUM_STATE] = {2, 0, 1, 8, 0,  4}; // {W=1, E=2, S=4, N=8}, for left turns only
	unsigned const to_right  [4] = {3, 2, 0, 1}; // {N, S, W, E}
	unsigned const to_left   [4] = {2, 3, 1, 0}; // {S, N, E, W}
	unsigned const other_lane[4] = {1, 0, 3, 2}; // {E, W, N, S}
	unsigned const conn_left[4] = {3,2,0,1}, conn_right[4] = {2,3,1,0};
	colorRGBA const stoplight_colors[3] = {GREEN, YELLOW, RED};
	colorRGBA const crosswalk_colors[3] = {WHITE, ORANGE, ORANGE};

	float stoplight_max_height();

	class stoplight_t {
		uint8_t num_conn, conn, cur_state;
		bool at_conn_road; // longer light times in this case
		float cur_state_ticks;
		// these are mutable because they are set during car update logic, where roads are supposed to be const
		mutable uint8_t car_waiting_sr, car_waiting_left;
		mutable bool blocked[4]; // Note: 4 bit flags corresponding to conn bits

		void next_state() {
			++cur_state;
			if (cur_state == NUM_STATE) {cur_state = 0;} // wraparound
		}
		void advance_state();
		bool any_blocked() const {return (blocked[0] || blocked[1] || blocked[2] || blocked[3]);}
		bool is_any_car_waiting_at_this_state() const;
		void find_state_with_waiting_car();
		void run_update_logic();
		float get_cur_state_time_secs() const {return (at_conn_road ? 2.0 : 1.0)*TICKS_PER_SECOND*state_times[cur_state];}
		void ffwd_to_future(float time_secs);
	public:
		stoplight_t(bool at_conn_road_) : num_conn(0), conn(0), cur_state(RED_LIGHT), at_conn_road(at_conn_road_), cur_state_ticks(0.0), car_waiting_sr(0), car_waiting_left(0) {
			reset_blocked();
		}
		void reset_blocked() {UNROLL_4X(blocked[i_] = 0;)}
		void mark_blocked(bool dim, bool dir) const {blocked[2*dim + dir] = 1;} // Note: not actually const, but blocked is mutable
		bool is_blocked(bool dim, bool dir) const {return (blocked[2*dim + dir] != 0);}
		void init(uint8_t num_conn_, uint8_t conn_);
		void next_frame();
		void notify_waiting_car(bool dim, bool dir, unsigned turn) const;
		bool red_light(bool dim, bool dir, unsigned turn) const;
		unsigned get_light_state(bool dim, bool dir, unsigned turn) const;
		bool can_walk(bool dim, bool dir) const;
		unsigned get_crosswalk_state(bool dim, bool dir) const;
		bool check_int_clear(unsigned orient, unsigned turn_dir) const;
		bool check_int_clear(car_t const &car) const {return check_int_clear(car.get_orient(), car.turn_dir);}
		bool can_turn_right_on_red(car_t const &car) const;
		string str() const;
		string label_str() const;
		colorRGBA get_stoplight_color(bool dim, bool dir, unsigned turn) const {return stoplight_colors[get_light_state(dim, dir, turn)];}
	};
} // end stoplight_ns


namespace streetlight_ns {

	colorRGBA const pole_color(BLACK); // so we don't have to worry about shadows
	colorRGBA const light_color(1.0, 0.9, 0.7, 1.0);
	float const light_height = 0.5; // in units of road width
	float const pole_radius  = 0.015;
	float const light_radius = 0.025;
	float const light_dist   = 3.0;
	float get_streetlight_height();

	struct streetlight_t {
		point pos; // bottom point
		vector3d dir;

		streetlight_t(point const &pos_, vector3d const &dir_) : pos(pos_), dir(dir_) {}
		bool is_lit(bool always_on) const {return (always_on || is_night(STREETLIGHT_ON_RAND*signed_rand_hash(pos.x + pos.y)));}
		point get_lpos() const;
		void draw(shader_t &s, vector3d const &xlate, bool shadow_only, bool is_local_shadow, bool always_on) const;
		void add_dlight(vector3d const &xlate, cube_t &lights_bcube, bool always_on) const;
		bool proc_sphere_coll(point &center, float radius, vector3d const &xlate, vector3d *cnorm) const;
		bool line_intersect(point const &p1, point const &p2, float &t) const;
	};
} // end streetlight_ns


struct streetlights_t {
	vector<streetlight_ns::streetlight_t> streetlights;

	void draw_streetlights(shader_t &s, vector3d const &xlate, bool shadow_only, bool always_on) const;
	void add_streetlight_dlights(vector3d const &xlate, cube_t &lights_bcube, bool always_on) const;
	bool proc_streetlight_sphere_coll(point &pos, float radius, vector3d const &xlate, vector3d *cnorm) const;
	bool line_intersect_streetlights(point const &p1, point const &p2, float &t) const;
};


struct draw_state_t {
	shader_t s;
	vector3d xlate;
protected:
	bool use_smap, use_bmap, shadow_only, use_dlights, emit_now;
	point_sprite_drawer_sized light_psd; // for car/traffic lights
	string label_str;
	point label_pos;
public:
	draw_state_t() : xlate(zero_vector), use_smap(0), use_bmap(0), shadow_only(0), use_dlights(0), emit_now(0) {}
	virtual void draw_unshadowed() {}
	void begin_tile(point const &pos, bool will_emit_now=0);
	void pre_draw(vector3d const &xlate_, bool use_dlights_, bool shadow_only_, bool always_setup_shader);
	void end_draw();
	virtual void post_draw();
	void ensure_shader_active();
	void draw_and_clear_light_flares();
	bool check_sphere_visible(point const &pos, float radius) const {return camera_pdu.sphere_visible_test((pos + xlate), radius);}
	bool check_cube_visible(cube_t const &bc, float dist_scale=1.0, bool shadow_only=0) const;
	static void set_cube_pts(cube_t const &c, float z1f, float z1b, float z2f, float z2b, bool d, bool D, point p[8]);
	static void set_cube_pts(cube_t const &c, float z1, float z2, bool d, bool D, point p[8]) {set_cube_pts(c, z1, z1, z2, z2, d, D, p);}
	static void set_cube_pts(cube_t const &c, bool d, bool D, point p[8]) {set_cube_pts(c, c.z1(), c.z2(), d, D, p);}
	static void rotate_pts(point const &center, float sine_val, float cos_val, int d, int e, point p[8]);
	void draw_cube(quad_batch_draw &qbd, color_wrapper const &cw, point const &center, point const p[8], bool skip_bottom, bool invert_normals=0, float tscale=0.0) const;
	void draw_cube(quad_batch_draw &qbd, cube_t const &c, color_wrapper const &cw, bool skip_bottom, float tscale=0.0) const;
	bool add_light_flare(point const &flare_pos, vector3d const &n, colorRGBA const &color, float alpha, float radius);
	void set_label_text(string const &str, point const &pos) {label_str = str; label_pos = pos;}
	void show_label_text();
}; // draw_state_t


struct road_isec_t : public cube_t {
	unsigned char num_conn, conn; // connected roads in {-x, +x, -y, +y} = {W, E, S, N} facing = car traveling {E, W, N, S}
	short conn_to_city;
	short rix_xy[4], conn_ix[4]; // road/segment index: pos=cur city road, neg=global road; always segment ix
	stoplight_ns::stoplight_t stoplight; // Note: not always needed, maybe should be by pointer/index?

	road_isec_t(cube_t const &c, int rx, int ry, unsigned char conn_, bool at_conn_road, short conn_to_city_=-1);
	tex_range_t get_tex_range(float ar) const;
	void make_4way(unsigned conn_to_city_);
	void next_frame() {stoplight.next_frame();}
	void notify_waiting_car(car_t const &car) const {stoplight.notify_waiting_car(car.dim, car.dir, car.turn_dir);}
	bool is_global_conn_int() const {return (rix_xy[0] < 0 || rix_xy[1] < 0 || rix_xy[2] < 0 || rix_xy[3] < 0);}
	bool red_light(car_t const &car) const {return stoplight.red_light(car.dim, car.dir, car.turn_dir);}
	bool red_or_yellow_light(car_t const &car) const {return (stoplight.get_light_state(car.dim, car.dir, car.turn_dir) != stoplight_ns::GREEN_LIGHT);}
	bool yellow_light(car_t const &car) const {return (stoplight.get_light_state(car.dim, car.dir, car.turn_dir) == stoplight_ns::YELLOW_LIGHT);}
	bool can_go_based_on_light(car_t const &car) const;
	bool is_orient_currently_valid(unsigned orient, unsigned turn_dir) const;
	unsigned get_dest_orient_for_car_in_isec(car_t const &car, bool is_entering) const;
	bool can_go_now(car_t const &car) const;
	bool is_blocked(car_t const &car) const {return (can_go_based_on_light(car) && !stoplight.check_int_clear(car));} // light is green but intersection is blocked
	bool has_left_turn_signal(unsigned orient) const;
	cube_t get_stoplight_cube(unsigned n) const;
	bool proc_sphere_coll(point &pos, point const &p_last, float radius, vector3d const &xlate, float dist, vector3d *cnorm) const;
	bool line_intersect(point const &p1, point const &p2, float &t) const;
	void draw_sl_block(quad_batch_draw &qbd, draw_state_t &dstate, point p[4], float h, unsigned state, bool draw_unlit, float flare_alpha, vector3d const &n, tex_range_t const &tr) const;
	void draw_stoplights(quad_batch_draw &qbd, draw_state_t &dstate, bool shadow_only) const;
};


struct road_connector_t : public road_t, public streetlights_t {
	road_t src_road;

	road_connector_t(road_t const &road) : road_t(road), src_road(road) {}
	float get_player_zval(point const &center, cube_t const &c) const;
	void add_streetlights(unsigned num_per_side, bool staggered, float dn_shift_mult, float za, float zb);
};

struct bridge_t : public road_connector_t {
	bool make_bridge;

	bridge_t(road_t const &road) : road_connector_t(road), make_bridge(0) {}
	void add_streetlights() {road_connector_t::add_streetlights(4, 0, 0.05, get_start_z(), get_end_z());} // 4 per side
	bool proc_sphere_coll(point &center, point const &prev, float sradius, float prev_frame_zval, vector3d const &xlate, vector3d *cnorm) const;
	bool line_intersect(point const &p1, point const &p2, float &t) const {return 0;} // TODO
};

struct tunnel_t : public road_connector_t {
	cube_t ends[2];
	float radius, height, facade_height[2];

	tunnel_t(road_t const &road) : road_connector_t(road), radius(0.0), height(0.0) {}
	bool enabled() const {return (radius > 0.0);}
	void init(point const &start, point const &end, float radius_, bool dim);
	void add_streetlights() {road_connector_t::add_streetlights(2, 1, -0.15, ends[0].z1(), ends[1].z1());} // 2 per side, staggered
	cube_t get_tunnel_bcube() const;
	void calc_top_bot_side_cubes(cube_t cubes[4]) const;
	bool check_mesh_disable(cube_t const &query_region) const {return (ends[0].intersects_xy(query_region) || ends[1].intersects_xy(query_region));} // check both ends
	bool proc_sphere_coll(point &center, point const &prev, float sradius, float prev_frame_zval, vector3d const &xlate, vector3d *cnorm) const;
	bool line_intersect(point const &p1, point const &p2, float &t) const;
};

struct range_pair_t {
	unsigned s, e; // Note: e is one past the end
	range_pair_t(unsigned s_=0, unsigned e_=0) : s(s_), e(e_) {}
	void update(unsigned v);
};

class road_draw_state_t : public draw_state_t {
	quad_batch_draw qbd_batched[NUM_RD_TYPES], qbd_sl, qbd_bridge;
	float ar;

	void draw_road_region_int(quad_batch_draw &cache, unsigned type_ix);
public:
	road_draw_state_t() : ar(1.0) {}
	void pre_draw(vector3d const &xlate_, bool use_dlights_, bool shadow_only);
	virtual void draw_unshadowed();
	virtual void post_draw();
	template<typename T> void add_road_quad(T const &r, quad_batch_draw &qbd, colorRGBA const &color) {add_flat_road_quad(r, qbd, color, ar);} // generic flat road case (plot/park)
	void add_road_quad(road_seg_t  const &r, quad_batch_draw &qbd, colorRGBA const &color) {r.add_road_quad(qbd, color, ar);} // road segment
	void add_road_quad(road_t      const &r, quad_batch_draw &qbd, colorRGBA const &color) {r.add_road_quad(qbd, color, ar/TRACKS_WIDTH);} // tracks

	template<typename T> void draw_road_region(vector<T> const &v, range_pair_t const &rp, quad_batch_draw &cache, unsigned type_ix) {
		if (rp.s == rp.e) return; // empty
		assert(rp.s <= rp.e);
		assert(rp.e <= v.size());
		assert(type_ix < NUM_RD_TYPES);
		colorRGBA const color(road_colors[type_ix]);

		if (cache.empty()) { // generate and cache quads
			for (unsigned i = rp.s; i < rp.e; ++i) {add_road_quad(v[i], cache, color);}
		}
		draw_road_region_int(cache, type_ix);
	}
	void draw_bridge(bridge_t const &bridge, bool shadow_only);
	void add_bridge_quad(point const pts[4], color_wrapper const &cw, float normal_scale);
	void draw_tunnel(tunnel_t const &tunnel, bool shadow_only);
	void draw_stoplights(vector<road_isec_t> const &isecs, bool shadow_only);
}; // road_draw_state_t

class car_draw_state_t : public draw_state_t {

	class occlusion_checker_t {
		building_occlusion_state_t state;
	public:
		void set_camera(pos_dir_up const &pdu);
		bool is_occluded(cube_t const &c);
	};

	quad_batch_draw qbds[3]; // unshadowed, shadowed, AO
	car_model_loader_t &car_model_loader;
	occlusion_checker_t occlusion_checker;
public:
	car_draw_state_t(car_model_loader_t &car_model_loader_) : car_model_loader(car_model_loader_) {}
	static float get_headlight_dist();
	colorRGBA get_headlight_color(car_t const &car) const;
	void pre_draw(vector3d const &xlate_, bool use_dlights_, bool shadow_only);
	virtual void draw_unshadowed();
	void add_car_headlights(vector<car_t> const &cars, vector3d const &xlate_, cube_t &lights_bcube);
	void gen_car_pts(car_t const &car, bool include_top, point pb[8], point pt[8]) const;
	void draw_car(car_t const &car, bool shadow_only, bool is_dlight_shadows);
	void add_car_headlights(car_t const &car, cube_t &lights_bcube);
}; // car_draw_state_t


class city_road_gen_t;

class car_manager_t {

	car_model_loader_t car_model_loader;

	struct car_block_t {
		unsigned start, cur_city, first_parked;
		car_block_t(unsigned s, unsigned c) : start(s), cur_city(c), first_parked(0) {}
	};
	struct comp_car_road {
		bool operator()(car_t const &c1, car_t const &c2) const {return (c1.cur_road < c2.cur_road);}
	};
	city_road_gen_t const &road_gen;
	vector<car_t> cars;
	vector<car_block_t> car_blocks;
	car_draw_state_t dstate;
	rand_gen_t rgen;
	vector<unsigned> entering_city;
	bool car_destroyed;

	cube_t const get_cb_bcube(car_block_t const &cb ) const;
	road_isec_t const &get_car_isec(car_t const &car) const;
	bool check_collision(car_t &c1, car_t &c2) const;
	void register_car_at_city(car_t const &car);
	void add_car();
	void get_car_ix_range_for_cube(vector<car_block_t>::const_iterator cb, cube_t const &bc, unsigned &start, unsigned &end) const;
	void remove_destroyed_cars();
	void update_cars();
	int find_next_car_after_turn(car_t &car);
public:
	car_manager_t(city_road_gen_t const &road_gen_) : road_gen(road_gen_), dstate(car_model_loader), car_destroyed(0) {}
	bool empty() const {return cars.empty();}
	void clear() {cars.clear(); car_blocks.clear();}
	void init_cars(unsigned num);
	void add_parked_cars(vector<car_t> const &new_cars) {cars.insert(cars.end(), new_cars.begin(), new_cars.end());}
	void finalize_cars();
	bool proc_sphere_coll(point &pos, point const &p_last, float radius, vector3d *cnorm) const;
	void destroy_cars_in_radius(point const &pos_in, float radius);
	bool get_color_at_xy(point const &pos, colorRGBA &color, int int_ret) const;
	car_t const *get_car_at(point const &p1, point const &p2) const;
	bool line_intersect_cars(point const &p1, point const &p2, float &t) const;
	void next_frame(float car_speed);
	void draw(int trans_op_mask, vector3d const &xlate, bool use_dlights, bool shadow_only);
	void add_car_headlights(vector3d const &xlate, cube_t &lights_bcube) {dstate.add_car_headlights(cars, xlate, lights_bcube);}
	void free_context() {car_model_loader.free_context();}
}; // car_manager_t


struct pedestrian_t {
	point pos;
	vector3d vel;
	float radius;
	unsigned city, plot;

	pedestrian_t(float radius_) : pos(all_zeros), vel(zero_vector), radius(radius_), city(0), plot(0) {}
	bool operator<(pedestrian_t const &ped) const {return ((city == ped.city) ? (plot < ped.plot) : (city < ped.city));} // currently only compares city + plot
	void move() {pos += vel*fticks;}
	bool is_valid_pos(cube_t const &plot_cube) const;
	bool try_place_in_plot(cube_t const &plot_cube, unsigned plot_id, rand_gen_t &rgen);
	void next_frame(cube_t const &plot_cube, rand_gen_t &rgen);
};


bool check_line_clip_update_t(point const &p1, point const &p2, float &t, cube_t const &c);
