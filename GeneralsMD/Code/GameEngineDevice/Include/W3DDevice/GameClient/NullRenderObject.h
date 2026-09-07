#pragma once

#include <string>

import Assets.Adapters.W3D.Null;
import Graphics.Scene.Models.Factory;

#include "WW3D2/RendObj.h"

class ChunkLoadClass;
class RenderInfoClass;

// Native W3D ownership and lifecycle calls for a NULL render object. NULL has
// no draw or collision-query work; its adapter only preserves identity, base
// RenderObj state, and the historical small bounds.
class NullRenderObject final : public RenderObjClass
{
public:
	NullRenderObject(const char *name = "NULL");
	explicit NullRenderObject(const Assets::W3D::W3DNullDescription &description);
	NullRenderObject(const NullRenderObject &source);
	NullRenderObject &operator=(const NullRenderObject &source);
	~NullRenderObject() override = default;

	RenderObjClass *Clone() const override;
	int Class_ID() const override { return RenderObjClass::CLASSID_NULL; }
	int Get_Num_Polys() const override { return 0; }
	const char *Get_Name() const override { return m_name.c_str(); }
	void Render(RenderInfoClass &rinfo) override;

	void Get_Obj_Space_Bounding_Sphere(SphereClass &sphere) const override;
	void Get_Obj_Space_Bounding_Box(AABoxClass &box) const override;

private:
	std::string m_name;
};

Graphics::ModelFactory<RenderObjClass> *Load_Null_Factory(ChunkLoadClass &cload);
Graphics::ModelFactory<RenderObjClass> *Create_Null_Render_Object_Factory();
