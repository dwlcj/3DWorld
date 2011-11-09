// 3D World - WaveFront Object File Reader
// by Frank Gennari
// 8/15/11
// Reference: http://en.wikipedia.org/wiki/Wavefront_.obj_file

#include "3DWorld.h"
#include "model3d.h"
#include <fstream>


extern bool recalc_model3d_normals;
extern model3ds all_models;


class object_file_reader {

protected:
	string filename;
	FILE *fp; // Note: we use a FILE* here instead of an ifstream because it's ~2.2x faster in MSVS
	static unsigned const MAX_CHARS = 1024;
	char buffer[MAX_CHARS];
	bool verbose;

	bool open_file() { // FIXME: what about multiple opens/reads?
		assert(!filename.empty());
		fp = fopen(filename.c_str(), "r");
		if (!fp) cerr << "Error: Could not open object file " << filename << endl;
		return (fp != 0);
	}

	void read_to_newline(ifstream &in) const {
		// FIXME: what about '\' line wraps?
		in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
	}

	void read_to_newline(FILE *fp_) const {
		while (1) {
			int const c(getc(fp_));
			if (c == '\n' || c == '\0' || c == EOF) return;
		}
		assert(0); // never gets here
	}

	void read_str_to_newline(FILE *fp_, string &str) const {
		assert(fp_);
		str.resize(0);

		while (1) {
			int const c(getc(fp_));
			if (c == '\n' || c == '\0' || c == EOF) break; // end of file or line
			if (!isspace(c) || !str.empty()) str.push_back(c);
		}
		return;
	}

	void normalize_index(int &ix, unsigned vect_sz) const {
		assert(ix != 0);

		if (ix < 0) { // negative (relative) index
			ix += vect_sz;
		}
		else { // positive (absolute) index
			--ix; // specified starting from 1, but we want starting from 0
		}
		assert((unsigned)ix < vect_sz);
	}

	bool read_point(point &p, int req_num=3) {
		return (fscanf(fp, "%f%f%f", &p.x, &p.y, &p.z) >= req_num);
	}

public:
	object_file_reader(string const &fn) : filename(fn), fp(NULL), verbose(0) {assert(!fn.empty());}
	~object_file_reader() {if (fp) fclose(fp);}

	bool read(vector<polygon_t> *ppts, geom_xform_t const &xf, bool verbose) {
		RESET_TIME;
		if (!open_file()) return 0;
		vector<point> v; // vertices
		char s[MAX_CHARS];
		polygon_t poly;

		while (fscanf(fp, "%s", s) == 1) {
			if (s[0] == '#') { // comment
				read_to_newline(fp); // ignore
			}
			else if (strcmp(s, "v") == 0) { // vertex
				v.push_back(point());
			
				if (!read_point(v.back())) {
					cerr << "Error reading vertex from object file " << filename << endl;
					return 0;
				}
				xf.xform_pos(v.back());
			}
			else if (strcmp(s, "f") == 0) { // face
				poly.resize(0);
				int ix(0);

				while (fscanf(fp, "%i", &ix) == 1) { // read vertex index
					normalize_index(ix, v.size());
					// only fill in the vertex (norm and tc will be unused)
					if (ppts) poly.push_back(vert_norm_tc(v[ix], zero_vector, 0.0, 0.0));
					int const c(getc(fp));

					if (c == '/') {
						fscanf(fp, "%i", &ix); // text coord index, ok to fail
						int const c2(getc(fp));

						if (c2 == '/') {
							fscanf(fp, "%i", &ix); // normal index, ok to fail
						}
						else ungetc(c2, fp);
					}
					else ungetc(c, fp);
				}
				if (ppts) split_polygon(poly, *ppts, POLY_COPLANAR_THRESH);
			}
			else {
				read_to_newline(fp); // ignore everything else
			}
		}
		PRINT_TIME("Polygons Load");
		if (verbose) cout << "v: " << v.size() << ", f: " << (ppts ? ppts->size() : 0) << endl;
		return 1;
	}
};


// ************************************************


class object_file_reader_model : public object_file_reader {

	string rel_path;
	model3d &model;

	string open_include_file(string const &fn, string const &type, ifstream &in_inc) const {
		assert(!fn.empty());
		in_inc.open(fn); // try absolute path
		if (in_inc.good()) return fn;
		in_inc.clear();
		string const rel_fn(rel_path + fn);
		in_inc.open(rel_fn); // try relative path
		if (in_inc.good()) return rel_fn;
		cerr << "Error: Could not open " << type << " file " << fn << " or " << rel_fn << endl;
		return string();
	}

	string get_path(string const &fn) const {
		for (unsigned pos = fn.size(); pos > 0; --pos) {
			if (fn[pos-1] == '\\' || fn[pos-1] == '/') {
				return string(fn.begin(), fn.begin()+pos);
			}
		}
		return string();
	}

	int get_texture(string const &fn, bool is_alpha_mask) {
		ifstream tex_in; // used only for determining file location
		string const fn_used(open_include_file(fn, "texture", tex_in));
		if (fn_used.empty()) return -1;
		tex_in.close();
		return model.tmgr.create_texture(fn_used, is_alpha_mask, verbose);
	}

	void check_and_bind(int &tid, string const &tfn, bool is_alpha_mask) {
		assert(tid < 0);
		tid = get_texture(tfn, is_alpha_mask);
	}

public:
	object_file_reader_model(string const &fn, model3d &model_) : object_file_reader(fn), model(model_) {
		rel_path = get_path(fn);
	}

	bool load_mat_lib(string const &fn) { // FIXME: cache these files or reload every time?
		ifstream mat_in;
		if (open_include_file(fn, "material library", mat_in).empty()) return 0;
		cout << "loading material library " << fn << endl;
		int cur_mat_id(-1); // not set
		material_t *cur_mat(0);
		string s, tfn;

		while (mat_in.good() && (mat_in >> s)) {
			assert(!s.empty());

			if (s[0] == '#') { // comment - can we have comments here?
				read_to_newline(mat_in); // ignore
			}
			else if (s == "newmtl") { // new material
				string material_name;

				if (!(mat_in >> material_name)) {
					cerr << "Error reading material name" << endl;
					return 0;
				}
				if (verbose) cout << "Material " << material_name << endl; // FIXME: too verbose?
				cur_mat_id =  model.get_material_ix(material_name, fn);
				cur_mat    = &model.get_material(cur_mat_id);
			}
			else if (s == "Ka") {
				assert(cur_mat);
				if (!(mat_in >> cur_mat->ka.R >> cur_mat->ka.G >> cur_mat->ka.B)) {cerr << "Error reading material Ka" << endl; return 0;}
			}
			else if (s == "Kd") {
				assert(cur_mat);
				if (!(mat_in >> cur_mat->kd.R >> cur_mat->kd.G >> cur_mat->kd.B)) {cerr << "Error reading material Kd" << endl; return 0;}
			}
			else if (s == "Ks") {
				assert(cur_mat);
				if (!(mat_in >> cur_mat->ks.R >> cur_mat->ks.G >> cur_mat->ks.B)) {cerr << "Error reading material Ks" << endl; return 0;}
			}
			else if (s == "Ke") {
				assert(cur_mat);
				if (!(mat_in >> cur_mat->ke.R >> cur_mat->ke.G >> cur_mat->ke.B)) {cerr << "Error reading material Ke" << endl; return 0;}
			}
			else if (s == "Ns") { // specular exponent
				assert(cur_mat);
				if (!(mat_in >> cur_mat->ns)) {cerr << "Error reading material Ns" << endl; return 0;}
			}
			else if (s == "Ni") { // index of refraction
				assert(cur_mat);
				if (!(mat_in >> cur_mat->ni)) {cerr << "Error reading material Ni" << endl; return 0;}
			}
			else if (s == "d") { // alpha
				assert(cur_mat);
				if (!(mat_in >> cur_mat->alpha)) {cerr << "Error reading material d" << endl; return 0;}
			}
			else if (s == "Tr") { // transmittance
				assert(cur_mat);
				if (!(mat_in >> cur_mat->tr)) {cerr << "Error reading material Tr" << endl; return 0;}
			}
			else if (s == "Tf") { // transmittion filter
				assert(cur_mat);
				if (!(mat_in >> cur_mat->tf.R >> cur_mat->tf.G >> cur_mat->tf.B)) {cerr << "Error reading material Tf" << endl; return 0;}
			}
			else if (s == "illum") { // 0 - 10
				assert(cur_mat);
				if (!(mat_in >> cur_mat->illum)) {cerr << "Error reading material Tr" << endl; return 0;}
			}
			else if (s == "map_Ka") {
				assert(cur_mat);
				if (!(mat_in >> tfn)) {cerr << "Error reading material map_Ka" << endl; return 0;}
				check_and_bind(cur_mat->a_tid, tfn, 0);
			}
			else if (s == "map_Kd") {
				assert(cur_mat);
				if (!(mat_in >> tfn)) {cerr << "Error reading material map_Kd" << endl; return 0;}
				check_and_bind(cur_mat->d_tid, tfn, 0);
			}
			else if (s == "map_Ks") {
				assert(cur_mat);
				if (!(mat_in >> tfn)) {cerr << "Error reading material map_Ks" << endl; return 0;}
				check_and_bind(cur_mat->s_tid, tfn, 0);
			}
			else if (s == "map_d") {
				assert(cur_mat);
				if (!(mat_in >> tfn)) {cerr << "Error reading material map_d" << endl; return 0;}
				check_and_bind(cur_mat->alpha_tid, tfn, 1);
			}
			else if (s == "map_bump" || s == "bump") { // should be ok if both are set
				assert(cur_mat);
				if (!(mat_in >> tfn)) {cerr << "Error reading material " << s << endl; return 0;}
				cur_mat->bump_tid = get_texture(tfn, 0); // can be set from both map_bump and bump
			}
			else if (s == "map_refl") {
				assert(cur_mat);
				if (!(mat_in >> tfn)) {cerr << "Error reading material map_refl" << endl; return 0;}
				check_and_bind(cur_mat->refl_tid, tfn, 0);
			}
			else if (s == "skip") { // skip this material
				assert(cur_mat);
				if (!(mat_in >> cur_mat->skip)) {cerr << "Error reading material skip" << endl; return 0;}
			}
			else {
				cerr << "Error: Undefined entry '" << s << "' in material library" << endl;
				return 0;
				//read_to_newline(mat_in); // ignore
			}
		}
		return 1;
	}


	bool read(vector<polygon_t> *ppts, geom_xform_t const &xf, bool verbose) {
		RESET_TIME;
		if (!open_file()) return 0;
		unsigned const block_size = (1 << 18); // 256K
		int cur_mat_id(-1);
		unsigned smoothing_group(0), num_faces(0), num_objects(0), num_groups(0), obj_group_id(0);
		vector<point> v; // vertices
		vector<vector3d> n; // normals
		vector<counted_normal> vn; // vertex normals
		vector<vector3d> tc; // texture coords
		deque<poly_data_block> pblocks;
		char s[MAX_CHARS];
		string material_name, mat_lib, group_name, object_name;

		while (fscanf(fp, "%s", s) == 1) {
			if (s[0] == 0) {cout << "empty/unparseable line?" << endl; continue;}
			if (s[0] == '#') { // comment
				read_to_newline(fp); // ignore
			}
			else if (strcmp(s, "f") == 0) { // face
				model.mark_mat_as_used(cur_mat_id);

				if (pblocks.empty() || pblocks.back().pts.size() >= block_size) {
					pblocks.push_back(poly_data_block());
				}
				poly_data_block &pb(pblocks.back());
				pb.polys.push_back(poly_header_t(cur_mat_id, obj_group_id));
				unsigned &npts(pb.polys.back().npts);
				unsigned const pix(pb.pts.size());
				int vix(0), tix(0), nix(0);

				while (fscanf(fp, "%i", &vix) == 1) { // read vertex index
					normalize_index(vix, v.size());
					vector3d normal(zero_vector), tex_coord(zero_vector); // normal will be recalculated later if zero
					int const c(getc(fp));

					if (c == '/') {
						if (fscanf(fp, "%i", &tix) == 1) { // read text coord index
							normalize_index(tix, tc.size());
							tex_coord = tc[tix];
						}
						int const c2(getc(fp));

						if (c2 == '/') {
							if (fscanf(fp, "%i", &nix) == 1 && !recalc_model3d_normals) { // read normal index
								normalize_index(nix, n.size());
								normal = n[nix];
							} // else the normal will be recalculated later
						}
						else ungetc(c2, fp);
					}
					else ungetc(c, fp);
					pb.pts.push_back(vert_norm_tc_ix(v[vix], normal, tex_coord.x, tex_coord.y, vix));
					++npts;
				} // end while vertex
				assert(npts >= 3);
				vector3d &normal(pb.polys.back().n);
				
				for (unsigned i = pix; i < pix+npts-2; ++i) { // find a nonzero normal
					normal = cross_product((pb.pts[i+1].v - pb.pts[i].v), (pb.pts[i+2].v - pb.pts[i].v)).get_norm(); // backwards?
					if (normal != zero_vector) break; // got a good normal
				}
				for (unsigned i = pix; i < pix+npts; ++i) {
					if (recalc_model3d_normals) {
						unsigned const ix(pb.pts[i].ix);
						assert((unsigned)ix < vn.size());

						if (vn[ix].is_valid() && dot_product(normal, vn[ix].get_norm()) < 0.25) { // normals in disagreement
							vn[ix] = zero_vector; // zero it out so that it becomes invalid later
						}
						else {
							vn[ix].add_normal(normal);
						}
					}
					else if (pb.pts[i].n == zero_vector) {
						pb.pts[i].n = normal;
					}
					//if (!smoothing_group) pv[i].n = normal;
				}
			}
			else if (strcmp(s, "v") == 0) { // vertex
				v.push_back(point());
				if (recalc_model3d_normals) vn.push_back(counted_normal()); // vertex normal
			
				if (!read_point(v.back())) {
					cerr << "Error reading vertex from object file " << filename << endl;
					return 0;
				}
				xf.xform_pos(v.back());
			}
			else if (strcmp(s, "vt") == 0) { // tex coord
				tc.push_back(vector3d());
			
				if (!read_point(tc.back(), 2)) {
					cerr << "Error reading texture coord from object file " << filename << endl;
					return 0;
				}
			}
			else if (strcmp(s, "vn") == 0) { // normal
				vector3d normal;
			
				if (!read_point(normal)) {
					cerr << "Error reading normal from object file " << filename << endl;
					return 0;
				}
				if (!recalc_model3d_normals) {
					xf.xform_pos_rm(normal);
					n.push_back(normal);
				}
			}
			else if (strcmp(s, "l") == 0) { // line
				read_to_newline(fp); // ignore
			}
			else if (strcmp(s, "o") == 0) { // object definition
				read_str_to_newline(fp, object_name); // can be empty?
				++num_objects;
				++obj_group_id;
			}
			else if (strcmp(s, "g") == 0) { // group
				read_str_to_newline(fp, group_name); // can be empty
				++num_groups;
				++obj_group_id;
			}
			else if (strcmp(s, "s") == 0) { // smoothing/shading (off/on or 0/1)
				if (fscanf(fp, "%u", &smoothing_group) != 1) {
					if (fscanf(fp, "%s", s) != 1 || strcmp(s, "off") != 0) {
						cerr << "Error reading smoothing group from object file " << filename << endl;
						return 0;
					}
					smoothing_group = 0;
				}
			}
			else if (strcmp(s, "usemtl") == 0) { // use material
				read_str_to_newline(fp, material_name);

				if (material_name.empty()) {
					cerr << "Error reading material from object file " << filename << endl;
					return 0;
				}
				cur_mat_id = model.find_material(material_name);
			}
			else if (strcmp(s, "mtllib") == 0) { // material library
				read_str_to_newline(fp, mat_lib);

				if (mat_lib.empty()) {
					cerr << "Error reading material library from object file " << filename << endl;
					return 0;
				}
				if (!load_mat_lib(mat_lib)) { // could cache loaded files, but they tend to not be reloaded and loading is fast anyway (since textures are cached)
					cerr << "Error reading material library file " << mat_lib << endl;
					return 0;
				}
			}
			else {
				cerr << "Error: Undefined entry '" << s << "' in object file " << filename << endl;
				return 0;
			}
		}
		PRINT_TIME("Object File Load");

		if (recalc_model3d_normals) {
			for (vector<counted_normal>::iterator i = vn.begin(); i != vn.end(); ++i) {
				if (!i->is_valid()) continue; // invalid, remains invalid
				*i /= (float)i->count;
				float const mag(i->mag());
				if (mag < 1E-6) {i->count = 0; continue;} // invalid
				assert(mag < 1.001);
				*i /= mag; // normalize
				i->count = (mag > 0.7); // stores the 'valid' state of the normal
			}
			for (deque<poly_data_block>::iterator i = pblocks.begin(); i != pblocks.end(); ++i) {
				unsigned pix(0);

				for (vector<poly_header_t>::iterator j = i->polys.begin(); j != i->polys.end(); ++j) {
					for (unsigned p = 0; p < j->npts; ++p) {
						vert_norm_tc_ix &V(i->pts[pix+p]);
						assert(V.ix < vn.size());
						counted_normal const &vert_norm(vn[V.ix]);
						V.n = ((j->n != zero_vector && !vert_norm.is_valid()) ? j->n : vert_norm);
					}
					pix += j->npts;
				}
			}
		}
		unsigned const nv(v.size()), nn(recalc_model3d_normals ? vn.size() : n.size()), ntc(tc.size()), num_blocks(pblocks.size());
		clear_cont(v);
		clear_cont(n);
		clear_cont(tc);
		clear_cont(vn);
		model.load_all_used_tids(); // need to load the textures here to get the colors
		PRINT_TIME("Model Texture Load");

		while (!pblocks.empty()) {
			poly_data_block const &pd(pblocks.back());
			unsigned pix(0);
			vntc_vect_t poly;

			for (vector<poly_header_t>::const_iterator j = pd.polys.begin(); j != pd.polys.end(); ++j) {
				poly.resize(j->npts);
				for (unsigned p = 0; p < j->npts; ++p) {poly[p] = pd.pts[pix+p];}
				num_faces += model.add_polygon(poly, j->mat_id, j->obj_id, ppts);
				pix += j->npts;
			}
			pblocks.pop_back();
		}
		model.remove_excess_cap();
		PRINT_TIME("Model3d Build");
		
		if (verbose) {
			cout << "verts: " << nv << ", normals: " << nn << ", tcs: " << ntc << ", faces: " << num_faces << ", objects: " << num_objects
				 << ", groups: " << num_groups << ", blocks: " << num_blocks << endl;
			cout << "bbox: "; model.get_bbox().print(); cout << endl;
			cout << "model stats: "; model.show_stats();
		}
		return 1;
	}
};


bool read_object_file(string const &filename, vector<polygon_t> *ppts, geom_xform_t const &xf,
	int def_tid, colorRGBA const &def_c, bool load_models, bool verbose)
{
	std::locale::global(std::locale("C"));
	setlocale(LC_ALL, "C");

	if (load_models) {
		all_models.push_back(model3d(all_models.tmgr, def_tid, def_c));
		object_file_reader_model reader(filename, all_models.back());
		return reader.read(ppts, xf, verbose);
	}
	else {
		object_file_reader reader(filename);
		return reader.read(ppts, xf, verbose);
	}
}


