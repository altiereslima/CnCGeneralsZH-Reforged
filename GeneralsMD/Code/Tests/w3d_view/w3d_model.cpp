/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "w3d_model.h"

#include <float.h>
#include <string.h>

#include <map>

#include "chunkio.h"
#include "ramfile.h"
#include "w3d_file.h"
#include "matrix3d.h"
#include "quat.h"
#include "vector3.h"

namespace {

std::string fixedName(const char *name, size_t length)
{
	size_t used = 0;
	while (used < length && name[used] != '\0') ++used;
	return std::string(name, used);
}

std::string upper(std::string s)
{
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] >= 'a' && s[i] <= 'z') s[i] = (char)(s[i] - 'a' + 'A');
	}
	return s;
}

struct Pivot
{
	std::string name;
	int parent;
	Matrix3D base;
	Matrix3D world;
};

struct Hierarchy
{
	std::string name;
	std::vector<Pivot> pivots;
};

struct SubObject
{
	unsigned bone;
	std::string name;	// "CONTAINER.MESH"
};

struct Lod
{
	std::string hierarchyName;
	std::vector<SubObject> objects;	// the highest level of detail only
	bool read;
};

// One mesh as the file has it, before it is placed.
struct Mesh
{
	std::string fullName;	// "CONTAINER.MESH", the name HLOD sub-objects use
	unsigned attributes;
	std::vector<W3dVectorStruct> positions;
	std::vector<W3dVectorStruct> normals;
	std::vector<W3dTriStruct> triangles;
	std::vector<W3dTexCoordStruct> texCoords;	// pass 0 stage 0
	std::vector<std::string> textures;
	std::vector<W3dShaderStruct> shaders;
	std::vector<W3dVertexMaterialStruct> vertexMaterials;
	std::vector<unsigned> textureIds;	// pass 0 stage 0: one for the mesh, or one per triangle
	std::vector<unsigned> shaderIds;		// pass 0: one, or one per triangle
	std::vector<unsigned> materialIds;	// pass 0: one, or one per vertex
	unsigned passCount;
};

template <typename T> void readArray(ChunkLoadClass &load, std::vector<T> *out)
{
	out->resize(load.Cur_Chunk_Length() / sizeof(T));
	if (!out->empty()) {
		load.Read(&(*out)[0], (uint32)(out->size() * sizeof(T)));
	}
}

void readTextures(ChunkLoadClass &load, Mesh *mesh)
{
	while (load.Open_Chunk()) {
		if (load.Cur_Chunk_ID() == W3D_CHUNK_TEXTURE) {
			std::string name;
			while (load.Open_Chunk()) {
				if (load.Cur_Chunk_ID() == W3D_CHUNK_TEXTURE_NAME) {
					std::vector<char> text(load.Cur_Chunk_Length() + 1, '\0');
					load.Read(&text[0], load.Cur_Chunk_Length());
					name = &text[0];
				}
				load.Close_Chunk();
			}
			mesh->textures.push_back(name);
		}
		load.Close_Chunk();
	}
}

void readVertexMaterials(ChunkLoadClass &load, Mesh *mesh)
{
	while (load.Open_Chunk()) {
		if (load.Cur_Chunk_ID() == W3D_CHUNK_VERTEX_MATERIAL) {
			W3dVertexMaterialStruct material;
			memset(&material, 0, sizeof(material));
			material.Diffuse.Set((uint8)255, (uint8)255, (uint8)255);
			material.Opacity = 1.0f;
			while (load.Open_Chunk()) {
				if (load.Cur_Chunk_ID() == W3D_CHUNK_VERTEX_MATERIAL_INFO) {
					load.Read(&material, sizeof(material));
				}
				load.Close_Chunk();
			}
			mesh->vertexMaterials.push_back(material);
		}
		load.Close_Chunk();
	}
}

void readMaterialPass(ChunkLoadClass &load, Mesh *mesh)
{
	const bool first = mesh->passCount == 0;
	++mesh->passCount;
	if (!first) {
		return;	// later passes (detail, lightmaps) are not drawn by the spike
	}
	bool firstStage = true;
	while (load.Open_Chunk()) {
		switch (load.Cur_Chunk_ID()) {
			case W3D_CHUNK_VERTEX_MATERIAL_IDS: readArray(load, &mesh->materialIds); break;
			case W3D_CHUNK_SHADER_IDS: readArray(load, &mesh->shaderIds); break;
			case W3D_CHUNK_TEXTURE_STAGE:
				if (firstStage) {
					firstStage = false;
					while (load.Open_Chunk()) {
						if (load.Cur_Chunk_ID() == W3D_CHUNK_TEXTURE_IDS) readArray(load, &mesh->textureIds);
						if (load.Cur_Chunk_ID() == W3D_CHUNK_STAGE_TEXCOORDS) readArray(load, &mesh->texCoords);
						load.Close_Chunk();
					}
				}
				break;
			default: break;
		}
		load.Close_Chunk();
	}
}

bool readMesh(ChunkLoadClass &load, Mesh *mesh)
{
	mesh->passCount = 0;
	bool haveHeader = false;
	while (load.Open_Chunk()) {
		switch (load.Cur_Chunk_ID()) {
			case W3D_CHUNK_MESH_HEADER3: {
				W3dMeshHeader3Struct header;
				load.Read(&header, sizeof(header));
				mesh->fullName = upper(fixedName(header.ContainerName, W3D_NAME_LEN) + "." + fixedName(header.MeshName, W3D_NAME_LEN));
				mesh->attributes = header.Attributes;
				haveHeader = true;
				break;
			}
			case W3D_CHUNK_VERTICES: readArray(load, &mesh->positions); break;
			case W3D_CHUNK_VERTEX_NORMALS: readArray(load, &mesh->normals); break;
			case W3D_CHUNK_TRIANGLES: readArray(load, &mesh->triangles); break;
			case W3D_CHUNK_SHADERS: readArray(load, &mesh->shaders); break;
			case W3D_CHUNK_TEXTURES: readTextures(load, mesh); break;
			case W3D_CHUNK_VERTEX_MATERIALS: readVertexMaterials(load, mesh); break;
			case W3D_CHUNK_MATERIAL_PASS: readMaterialPass(load, mesh); break;
			default: break;
		}
		load.Close_Chunk();
	}
	return haveHeader;
}

// HTreeClass::read_pivots, minus what a static pose does not need (fixups, visibility).
bool readHierarchy(ChunkLoadClass &load, Hierarchy *tree)
{
	W3dHierarchyStruct header;
	memset(&header, 0, sizeof(header));
	while (load.Open_Chunk()) {
		if (load.Cur_Chunk_ID() == W3D_CHUNK_HIERARCHY_HEADER) {
			load.Read(&header, sizeof(header));
			tree->name = upper(fixedName(header.Name, W3D_NAME_LEN));
		} else if (load.Cur_Chunk_ID() == W3D_CHUNK_PIVOTS) {
			const unsigned count = load.Cur_Chunk_Length() / sizeof(W3dPivotStruct);
			for (unsigned i = 0; i < count; ++i) {
				W3dPivotStruct piv;
				load.Read(&piv, sizeof(piv));
				Pivot pivot;
				pivot.name = fixedName(piv.Name, W3D_NAME_LEN);
				pivot.parent = piv.ParentIdx == 0xffffffffu ? -1 : (int)piv.ParentIdx;
				Matrix3D rotation;
				pivot.base.Make_Identity();
				pivot.base.Translate(Vector3(piv.Translation.X, piv.Translation.Y, piv.Translation.Z));
				pivot.base.postMul(Build_Matrix3D(Quaternion(piv.Rotation.Q[0], piv.Rotation.Q[1], piv.Rotation.Q[2], piv.Rotation.Q[3]), rotation));
				tree->pivots.push_back(pivot);
			}
		}
		load.Close_Chunk();
	}
	// A pre-3.0 tree has no root node; the Crusader's era does, and the spike does not fake one.
	if (header.Version < W3D_MAKE_VERSION(3, 0)) {
		return false;
	}
	for (size_t i = 0; i < tree->pivots.size(); ++i) {
		Pivot &pivot = tree->pivots[i];
		if (pivot.parent >= 0 && (size_t)pivot.parent < i) {
			Matrix3D::Multiply(tree->pivots[pivot.parent].world, pivot.base, &pivot.world);
		} else {
			pivot.world = pivot.base;
		}
	}
	return true;
}

// HLodDefClass::read_header and read_lod_array; only the first LOD array, which is the highest
// detail (the exporter writes them from the top down).
void readHlod(ChunkLoadClass &load, Lod *lod)
{
	bool haveLod = false;
	while (load.Open_Chunk()) {
		if (load.Cur_Chunk_ID() == W3D_CHUNK_HLOD_HEADER) {
			W3dHLodHeaderStruct header;
			load.Read(&header, sizeof(header));
			lod->hierarchyName = upper(fixedName(header.HierarchyName, W3D_NAME_LEN));
		} else if (load.Cur_Chunk_ID() == W3D_CHUNK_HLOD_LOD_ARRAY && !haveLod) {
			haveLod = true;
			while (load.Open_Chunk()) {
				if (load.Cur_Chunk_ID() == W3D_CHUNK_HLOD_SUB_OBJECT) {
					W3dHLodSubObjectStruct sub;
					load.Read(&sub, sizeof(sub));
					SubObject object;
					object.bone = sub.BoneIndex;
					object.name = upper(fixedName(sub.Name, sizeof(sub.Name)));
					lod->objects.push_back(object);
				}
				load.Close_Chunk();
			}
		}
		load.Close_Chunk();
	}
	lod->read = haveLod;
}

float byteColour(uint8 value) { return (float)value / 255.0f; }

DrawState stateFor(const Mesh &mesh, unsigned shaderId)
{
	DrawState state;
	memset(&state, 0, sizeof(state));
	state.depthWrite = true;
	state.twoSided = (mesh.attributes & W3D_MESH_FLAG_TWO_SIDED) != 0;
	if (shaderId < mesh.shaders.size()) {
		const W3dShaderStruct &shader = mesh.shaders[shaderId];
		state.alphaTest = shader.AlphaTest == W3DSHADER_ALPHATEST_ENABLE;
		state.depthWrite = shader.DepthMask != W3DSHADER_DEPTHMASK_WRITE_DISABLE;
		state.blend = shader.SrcBlend == W3DSHADER_SRCBLENDFUNC_SRC_ALPHA
			&& shader.DestBlend == W3DSHADER_DESTBLENDFUNC_ONE_MINUS_SRC_ALPHA;
		state.additive = shader.SrcBlend == W3DSHADER_SRCBLENDFUNC_ONE && shader.DestBlend == W3DSHADER_DESTBLENDFUNC_ONE;
	}
	return state;
}

unsigned idFor(const std::vector<unsigned> &ids, size_t index)
{
	if (ids.empty()) return 0;
	return ids.size() == 1 ? ids[0] : (index < ids.size() ? ids[index] : ids[0]);
}

} // namespace

bool DrawState::operator<(const DrawState &o) const { return memcmp(this, &o, sizeof(*this)) < 0; }
bool DrawState::operator==(const DrawState &o) const { return memcmp(this, &o, sizeof(*this)) == 0; }

bool loadW3dModel(const std::vector<unsigned char> &file, Model *model, std::string *why)
{
	std::vector<unsigned char> copy(file);	// RAMFileClass takes a non-const buffer
	RAMFileClass ram(copy.empty() ? NULL : &copy[0], (int)copy.size());
	ram.Open();
	ChunkLoadClass load(&ram);

	std::vector<Mesh> meshes;
	std::vector<Hierarchy> trees;
	std::vector<Lod> lods;
	while (load.Open_Chunk()) {
		switch (load.Cur_Chunk_ID()) {
			case W3D_CHUNK_MESH: {
				Mesh mesh;
				if (readMesh(load, &mesh)) meshes.push_back(mesh);
				break;
			}
			case W3D_CHUNK_HIERARCHY: {
				Hierarchy tree;
				if (readHierarchy(load, &tree)) trees.push_back(tree);
				else model->notes.push_back("a hierarchy older than W3D 3.0 was skipped");
				break;
			}
			case W3D_CHUNK_HLOD: {
				Lod lod;
				readHlod(load, &lod);
				if (lod.read) lods.push_back(lod);
				break;
			}
			default: break;
		}
		load.Close_Chunk();
	}

	if (lods.empty() || trees.empty()) {
		*why = "no HLOD with a hierarchy in this file; the spike draws HLOD models only";
		return false;
	}
	const Lod &lod = lods[0];
	const Hierarchy *tree = NULL;
	for (size_t i = 0; i < trees.size(); ++i) {
		if (trees[i].name == lod.hierarchyName) tree = &trees[i];
	}
	if (tree == NULL) {
		*why = "the HLOD's hierarchy " + lod.hierarchyName + " is not in this file";
		return false;
	}
	model->name = lod.hierarchyName;

	std::map<std::string, const Mesh *> byName;
	for (size_t i = 0; i < meshes.size(); ++i) byName[meshes[i].fullName] = &meshes[i];

	for (int axis = 0; axis < 3; ++axis) {
		model->boundsMin[axis] = FLT_MAX;
		model->boundsMax[axis] = -FLT_MAX;
	}

	for (size_t o = 0; o < lod.objects.size(); ++o) {
		const SubObject &object = lod.objects[o];
		std::map<std::string, const Mesh *>::const_iterator found = byName.find(object.name);
		if (found == byName.end()) {
			model->notes.push_back(object.name + ": not a mesh in this file (a box or aggregate?), skipped");
			continue;
		}
		const Mesh &mesh = *found->second;
		if ((mesh.attributes & W3D_MESH_FLAG_HIDDEN) != 0) {
			model->notes.push_back(object.name + ": hidden by default, skipped");
			continue;
		}
		if ((mesh.attributes & W3D_MESH_FLAG_GEOMETRY_TYPE_MASK) != W3D_MESH_FLAG_GEOMETRY_TYPE_NORMAL) {
			model->notes.push_back(object.name + ": not rigid geometry (skin or camera-aligned), skipped");
			continue;
		}
		if (object.bone >= tree->pivots.size()) {
			model->notes.push_back(object.name + ": bone out of range, skipped");
			continue;
		}
		if (mesh.passCount > 1) {
			model->notes.push_back(object.name + ": only the first of its material passes is drawn");
		}
		const Matrix3D &bone = tree->pivots[object.bone].world;

		const unsigned base = (unsigned)model->vertices.size();
		for (size_t v = 0; v < mesh.positions.size(); ++v) {
			Vector3 p;
			Matrix3D::Transform_Vector(bone, Vector3(mesh.positions[v].X, mesh.positions[v].Y, mesh.positions[v].Z), &p);
			Vector3 n(0.0f, 0.0f, 1.0f);
			if (v < mesh.normals.size()) {
				Matrix3D::Rotate_Vector(bone, Vector3(mesh.normals[v].X, mesh.normals[v].Y, mesh.normals[v].Z), &n);
			}
			ModelVertex vertex;
			vertex.position[0] = p.X; vertex.position[1] = p.Y; vertex.position[2] = p.Z;
			vertex.normal[0] = n.X; vertex.normal[1] = n.Y; vertex.normal[2] = n.Z;
			// meshmdlio.cpp's read_stage_texcoords: the file stores V bottom-up.
			vertex.texCoord[0] = v < mesh.texCoords.size() ? mesh.texCoords[v].U : 0.0f;
			vertex.texCoord[1] = v < mesh.texCoords.size() ? 1.0f - mesh.texCoords[v].V : 0.0f;
			model->vertices.push_back(vertex);
			const float coords[3] = { p.X, p.Y, p.Z };
			for (int axis = 0; axis < 3; ++axis) {
				if (coords[axis] < model->boundsMin[axis]) model->boundsMin[axis] = coords[axis];
				if (coords[axis] > model->boundsMax[axis]) model->boundsMax[axis] = coords[axis];
			}
		}

		// Group the triangles by (texture, shader); per-triangle ids split a mesh into several draws.
		std::map<std::pair<unsigned, unsigned>, std::vector<unsigned> > groups;
		for (size_t t = 0; t < mesh.triangles.size(); ++t) {
			const W3dTriStruct &tri = mesh.triangles[t];
			if (tri.Vindex[0] >= mesh.positions.size() || tri.Vindex[1] >= mesh.positions.size() || tri.Vindex[2] >= mesh.positions.size()) {
				continue;
			}
			std::vector<unsigned> &group = groups[std::make_pair(idFor(mesh.textureIds, t), idFor(mesh.shaderIds, t))];
			group.push_back(base + tri.Vindex[0]);
			group.push_back(base + tri.Vindex[1]);
			group.push_back(base + tri.Vindex[2]);
		}
		const unsigned materialId = idFor(mesh.materialIds, 0);
		const std::string meshName = object.name.substr(0, object.name.find('.'));
		for (std::map<std::pair<unsigned, unsigned>, std::vector<unsigned> >::const_iterator g = groups.begin(); g != groups.end(); ++g) {
			ModelDraw draw;
			draw.meshName = object.name;
			draw.textureName = g->first.first < mesh.textures.size() && !mesh.texCoords.empty() ? mesh.textures[g->first.first] : std::string();
			draw.diffuse[0] = draw.diffuse[1] = draw.diffuse[2] = draw.diffuse[3] = 1.0f;
			if (materialId < mesh.vertexMaterials.size()) {
				const W3dVertexMaterialStruct &material = mesh.vertexMaterials[materialId];
				draw.diffuse[0] = byteColour(material.Diffuse.R);
				draw.diffuse[1] = byteColour(material.Diffuse.G);
				draw.diffuse[2] = byteColour(material.Diffuse.B);
				draw.diffuse[3] = material.Opacity;
			}
			draw.houseColor = upper(meshName).compare(0, 10, "HOUSECOLOR") == 0;
			draw.state = stateFor(mesh, g->first.second);
			draw.firstIndex = (unsigned)model->indices.size();
			draw.indexCount = (unsigned)g->second.size();
			model->indices.insert(model->indices.end(), g->second.begin(), g->second.end());
			model->draws.push_back(draw);
		}
	}

	if (model->draws.empty()) {
		*why = "the HLOD's top level of detail placed no drawable mesh";
		return false;
	}
	return true;
}
