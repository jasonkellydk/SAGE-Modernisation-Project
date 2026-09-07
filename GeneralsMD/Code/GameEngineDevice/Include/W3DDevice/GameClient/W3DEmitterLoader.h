#pragma once
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
import Graphics.Scene.Models.Factory;

class RenderObjClass;
class ChunkLoadClass;
Graphics::ModelFactory<RenderObjClass>* Load_ParticleEmitter_Factory(ChunkLoadClass& source);
