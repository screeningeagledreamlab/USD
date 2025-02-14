//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//

#include "pxr/pxr.h"

#include "pxr/imaging/garch/glApi.h"

#include "pxr/usdImaging/usdImagingGL/unitTestGLDrawing.h"

#include "pxr/base/arch/systemInfo.h"
#include "pxr/base/gf/bbox3d.h"
#include "pxr/base/gf/frustum.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/range3d.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/trace/trace.h"

#include "pxr/imaging/glf/simpleLightingContext.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/bboxCache.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "pxr/usdImaging/usdImaging/unitTestHelper.h"
#include "pxr/usdImaging/usdImaging/tokens.h"

#include "pxr/usdImaging/usdImagingGL/engine.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <fstream>

PXR_NAMESPACE_USING_DIRECTIVE

using UsdImagingGLEngineSharedPtr = std::shared_ptr<class UsdImagingGLEngine>;

class My_TestGLDrawing : public UsdImagingGL_UnitTestGLDrawing {
public:
    My_TestGLDrawing() {
        _mousePos[0] = _mousePos[1] = 0;
        _mouseButton[0] = _mouseButton[1] = _mouseButton[2] = false;
        _rotate[0] = _rotate[1] = 0;
        _translate[0] = _translate[1] = _translate[2] = 0;
    }

    // UsdImagingGL_UnitTestGLDrawing overrides
    virtual void InitTest();
    virtual void DrawTest(bool offscreen);
    virtual void ShutdownTest();

    virtual void MousePress(int button, int x, int y, int modKeys);
    virtual void MouseRelease(int button, int x, int y, int modKeys);
    virtual void MouseMove(int x, int y, int modKeys);

private:
    UsdStageRefPtr _stage;
    UsdImagingGLEngineSharedPtr _engine;
    GlfSimpleLightingContextRefPtr _lightingContext;

    float _rotate[2];
    float _translate[3];
    int _mousePos[2];
    bool _mouseButton[3];
};

GLuint vao;

void
My_TestGLDrawing::InitTest()
{
    TRACE_FUNCTION();
    
    std::cout << "My_TestGLDrawing::InitTest()\n";
    _stage = UsdStage::Open(GetStageFilePath(),
        IsEnabledUnloadedAsBounds() ? UsdStage::LoadNone : UsdStage::LoadAll);

    SdfPathVector excludedPaths;
    if (UsdImagingGLEngine::IsHydraEnabled()) {
        std::cout << "Using HD Renderer.\n";
        _engine.reset(new UsdImagingGLEngine(
            _stage->GetPseudoRoot().GetPath(), excludedPaths));
        if (!_GetRenderer().IsEmpty()) {
            if (!_engine->SetRendererPlugin(_GetRenderer())) {
                std::cerr << "Couldn't set renderer plugin: " <<
                    _GetRenderer().GetText() << std::endl;
                exit(-1);
            } else {
                std::cout << "Renderer plugin: " << _GetRenderer().GetText()
                    << std::endl;
            }
        }
    } else{
        std::cout << "Using Reference Renderer.\n"; 
        _engine.reset(
            new UsdImagingGLEngine(_stage->GetPseudoRoot().GetPath(), 
                    excludedPaths));
    }

    for (const auto &renderSetting : GetRenderSettings()) {
        _engine->SetRendererSetting(TfToken(renderSetting.first),
                                    renderSetting.second);
    }

    std::cout << glGetString(GL_VENDOR) << "\n";
    std::cout << glGetString(GL_RENDERER) << "\n";
    std::cout << glGetString(GL_VERSION) << "\n";

    if (_ShouldFrameAll()) {
        TfTokenVector purposes;
        purposes.push_back(UsdGeomTokens->default_);
        if (IsShowGuides()) {
            purposes.push_back(UsdGeomTokens->guide);
        }
        if (IsShowProxy()) {
            purposes.push_back(UsdGeomTokens->proxy);
        }
        if (IsShowRender()) {
            purposes.push_back(UsdGeomTokens->render);
        }

        // Extent hints are sometimes authored as an optimization to avoid
        // computing bounds, they are particularly useful for some tests where
        // there is no bound on the first frame.
        bool useExtentHints = true;
        UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(), purposes, useExtentHints);

        GfBBox3d bbox = bboxCache.ComputeWorldBound(_stage->GetPseudoRoot());
        GfRange3d world = bbox.ComputeAlignedRange();

        GfVec3d worldCenter = (world.GetMin() + world.GetMax()) / 2.0;
        double worldSize = world.GetSize().GetLength();

        std::cerr << "worldCenter: " << worldCenter << "\n";
        std::cerr << "worldSize: " << worldSize << "\n";
        if (UsdGeomGetStageUpAxis(_stage) == UsdGeomTokens->z) {
            // transpose y and z centering translation
            _translate[0] = -worldCenter[0];
            _translate[1] = -worldCenter[2];
            _translate[2] = -worldCenter[1] - worldSize;
        } else {
            _translate[0] = -worldCenter[0];
            _translate[1] = -worldCenter[1];
            _translate[2] = -worldCenter[2] - worldSize;
        }
    } else {
        _translate[0] = GetTranslate()[0];
        _translate[1] = GetTranslate()[1];
        _translate[2] = GetTranslate()[2];
    }

    if(IsEnabledTestLighting()) {
        _lightingContext = GlfSimpleLightingContext::New();
        if(UsdImagingGLEngine::IsHydraEnabled()) {
            // set same parameter as GlfSimpleLightingContext::SetStateFromOpenGL
            // OpenGL defaults
            if (!IsEnabledSceneLights()) {
                GlfSimpleLight light;
                if (IsEnabledCameraLight()) {
                    light.SetPosition(GfVec4f(_translate[0], _translate[2], _translate[1], 0));
                } else {
                    light.SetPosition(GfVec4f(0, -.5, .5, 0));
                }
                light.SetDiffuse(GfVec4f(1,1,1,1));
                light.SetAmbient(GfVec4f(0,0,0,1));
                light.SetSpecular(GfVec4f(1,1,1,1));
                GlfSimpleLightVector lights;
                lights.push_back(light);
                _lightingContext->SetLights(lights);
            }

            GlfSimpleMaterial material;
            material.SetAmbient(GfVec4f(0.2, 0.2, 0.2, 1.0));
            material.SetDiffuse(GfVec4f(0.8, 0.8, 0.8, 1.0));
            material.SetSpecular(GfVec4f(0,0,0,1));
            material.SetShininess(0.0001f);
            _lightingContext->SetMaterial(material);
            _lightingContext->SetSceneAmbient(GfVec4f(0.2,0.2,0.2,1.0));
        } else {
            glEnable(GL_LIGHTING);
            glEnable(GL_LIGHT0);
            if (IsEnabledCameraLight()) {
                float position[4] = {_translate[0], _translate[2], _translate[1], 0};
                glLightfv(GL_LIGHT0, GL_POSITION, position);
            } else {
                float position[4] = {0,-.5,.5,0};
                glLightfv(GL_LIGHT0, GL_POSITION, position);
            }
        }
    }
}

void
My_TestGLDrawing::DrawTest(bool offscreen)
{
    TRACE_FUNCTION();

    std::cout << "My_TestGLDrawing::DrawTest()\n";

    TfStopwatch renderTime;

    HdPerfLog& perfLog = HdPerfLog::GetInstance();
    perfLog.Enable();
    
    // Reset all counters we care about.
    perfLog.ResetCache(HdTokens->extent);
    perfLog.ResetCache(HdTokens->points);
    perfLog.ResetCache(HdTokens->topology);
    perfLog.ResetCache(HdTokens->transform);
    perfLog.SetCounter(UsdImagingTokens->usdVaryingExtent, 0);
    perfLog.SetCounter(UsdImagingTokens->usdVaryingPrimvar, 0);
    perfLog.SetCounter(UsdImagingTokens->usdVaryingTopology, 0);
    perfLog.SetCounter(UsdImagingTokens->usdVaryingVisibility, 0);
    perfLog.SetCounter(UsdImagingTokens->usdVaryingXform, 0);

    const int width = GetWidth();
    const int height = GetHeight();

    if (GetCameraPath().empty()) {
        GfMatrix4d viewMatrix(1.0);
        viewMatrix *= GfMatrix4d().SetRotate(GfRotation(GfVec3d(0, 1, 0), _rotate[0]));
        viewMatrix *= GfMatrix4d().SetRotate(GfRotation(GfVec3d(1, 0, 0), _rotate[1]));
        viewMatrix *= GfMatrix4d().SetTranslate(GfVec3d(_translate[0], _translate[1], _translate[2]));

        GfMatrix4d modelViewMatrix = viewMatrix; 
        if (UsdGeomGetStageUpAxis(_stage) == UsdGeomTokens->z) {
            // rotate from z-up to y-up
            modelViewMatrix = 
                GfMatrix4d().SetRotate(GfRotation(GfVec3d(1.0,0.0,0.0), -90.0)) *
                modelViewMatrix;
        }

        const double aspectRatio = double(width)/height;
        GfFrustum frustum;
        frustum.SetPerspective(60.0, aspectRatio, 1, 100000.0);
        const GfMatrix4d projMatrix = frustum.ComputeProjectionMatrix();

        _engine->SetCameraState(modelViewMatrix, projMatrix);
    } else {
        _engine->SetCameraPath(SdfPath(GetCameraPath()));
    }

    const CameraUtilFraming framing(
        GetDisplayWindow(), GetDataWindow(), GetPixelAspectRatio());
    if (framing.IsValid()) {
        _engine->SetRenderBufferSize(GfVec2i(width, height));
        _engine->SetFraming(framing);
        _engine->SetOverrideWindowPolicy({true, CameraUtilFit});
    } else {
        const GfVec4d viewport(0, 0, width, height);
        _engine->SetRenderViewport(viewport);
    }
 
    bool const useAovs = !GetRendererAov().IsEmpty();
    GfVec4f fboClearColor = useAovs? GfVec4f(0.0f) : GetClearColor();
    GLfloat clearDepth[1] = { 1.0f };
    bool const clearOnlyOnce = ShouldClearOnce();
    bool cleared = false;

    UsdImagingGLRenderParams params;
    params.drawMode = GetDrawMode();
    params.enableLighting = IsEnabledTestLighting();
    params.enableIdRender = IsEnabledIdRender();
    params.enableSceneMaterials = IsEnabledSceneMaterials();
    params.complexity = _GetComplexity();
    params.cullStyle = GetCullStyle();
    params.showGuides = IsShowGuides();
    params.showRender = IsShowRender();
    params.showProxy = IsShowProxy();
    params.clearColor = GetClearColor();

    if (IsEnabledUnloadedAsBounds()) {
        _SetDisplayUnloadedPrimsWithBounds(_engine.get(), true);
    }

    glViewport(0, 0, width, height);

    glEnable(GL_DEPTH_TEST);

    if (useAovs) {
        _engine->SetRendererAov(GetRendererAov());
    }

    if(IsEnabledTestLighting()) {
        if(UsdImagingGLEngine::IsHydraEnabled()) {
            _engine->SetLightingState(_lightingContext);
        } else {
            _lightingContext->SetStateFromOpenGL();
            _engine->SetLightingState(_lightingContext);
        }
    }

    if (PresentDisabled()) {
        _engine->SetEnablePresentation(false);
    }

    if (!GetClipPlanes().empty()) {
        params.clipPlanes = GetClipPlanes();
        for (size_t i=0; i<GetClipPlanes().size(); ++i) {
            glEnable(GL_CLIP_PLANE0 + i);
        }
    }

    for (double const &t : GetTimes()) {
        UsdTimeCode time = t;
        if (t == -999) {
            time = UsdTimeCode::Default();
        }

        params.frame = time;

        // Make sure we render to convergence.
        TfErrorMark mark;
        int convergenceIterations = 0;

        {
            TRACE_FUNCTION_SCOPE("test profile: renderTime");

            renderTime.Start();

            do {
                TRACE_FUNCTION_SCOPE("iteration render convergence");
                
                convergenceIterations++;

                if (cleared && clearOnlyOnce) {
                    // Don't clear the FBO
                } else {
                    glClearBufferfv(GL_COLOR, 0, fboClearColor.data());
                    glClearBufferfv(GL_DEPTH, 0, clearDepth);

                    cleared = true;
                }
                
                _engine->Render(_stage->GetPseudoRoot(), params);
            } while (!_engine->IsConverged());
        
            {
                TRACE_FUNCTION_SCOPE("glFinish");
                glFinish();
            }

            renderTime.Stop();            
        }

        TF_VERIFY(mark.IsClean(), "Errors occurred while rendering!");

        std::cout << "Iterations to convergence: " << convergenceIterations << std::endl;
        std::cout << "itemsDrawn " << perfLog.GetCounter(HdTokens->itemsDrawn) << std::endl;
        std::cout << "totalItemCount " << perfLog.GetCounter(HdTokens->totalItemCount) << std::endl;

        std::string imageFilePath = GetOutputFilePath();
        if (!imageFilePath.empty()) {
            if (time != UsdTimeCode::Default()) {
                std::stringstream suffix;
                suffix << "_" << std::setw(3) << std::setfill('0') << params.frame << ".png";
                imageFilePath = TfStringReplace(imageFilePath, ".png", suffix.str());
            }
            std::cout << imageFilePath << "\n";
            WriteToFile("color", imageFilePath);
        }
    }

    if (!GetPerfStatsFile().empty()) {
        std::ofstream perfstatsRaw(GetPerfStatsFile(), std::ofstream::out);
        if (TF_VERIFY(perfstatsRaw)) {
            perfstatsRaw << "{ 'profile'  : 'renderTime', "
                         << "   'metric'  : 'time', "
                         << "   'value'   : " << renderTime.GetSeconds() << ", "
                         << "   'samples' : " << GetTimes().size() << " }" << std::endl;
        }
    }
}

void
My_TestGLDrawing::ShutdownTest()
{
    std::cout << "My_TestGLDrawing::ShutdownTest()\n";
}

void
My_TestGLDrawing::MousePress(int button, int x, int y, int modKeys)
{
    _mouseButton[button] = 1;
    _mousePos[0] = x;
    _mousePos[1] = y;
}

void
My_TestGLDrawing::MouseRelease(int button, int x, int y, int modKeys)
{
    _mouseButton[button] = 0;
}

void
My_TestGLDrawing::MouseMove(int x, int y, int modKeys)
{
    int dx = x - _mousePos[0];
    int dy = y - _mousePos[1];

    if (_mouseButton[0]) {
        _rotate[0] += dx;
        _rotate[1] += dy;
    } else if (_mouseButton[1]) {
        _translate[0] += dx;
        _translate[1] -= dy;
    } else if (_mouseButton[2]) {
        _translate[2] += dx;
    }

    _mousePos[0] = x;
    _mousePos[1] = y;
}

void
BasicTest(int argc, char *argv[])
{
    My_TestGLDrawing driver;
    driver.RunTest(argc, argv);
}

int main(int argc, char *argv[])
{
    BasicTest(argc, argv);
    std::cout << "OK" << std::endl;
}
