#include "ViewerApplication.hpp"

#include <iostream>
#include <numeric>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/io.hpp>

#include "utils/cameras.hpp"
#include "utils/gltf.hpp"
#include "utils/images.hpp"

#include <stb_image_write.h>
#include <tiny_gltf.h>

void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
  if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE)
  {
    glfwSetWindowShouldClose(window, 1);
  }
}

bool ViewerApplication::loadGltfFile(tinygltf::Model &model)
{
  tinygltf::TinyGLTF loader;
  std::string warn;
  std::string err;
  bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, m_gltfFilePath.string());

  if (!warn.empty())
  {
    std::cout << "Warn : " << warn << std::endl;
  }

  if (!err.empty())
  {
    std::cerr << "Error : " << err << std::endl;
  }

  if (!ret)
  {
    std::cerr << "Failed to parse the glTF file" << std::endl;
  }

  return ret;
}

std::vector<GLuint> ViewerApplication::createBufferObjects(const tinygltf::Model &model)
{
  std::vector<GLuint> buffers(model.buffers.size(), 0);
  glGenBuffers(buffers.size(), buffers.data());
  for (size_t i = 0; i < buffers.size(); i++)
  {
    glBindBuffer(GL_ARRAY_BUFFER, buffers[i]);
    glBufferStorage(GL_ARRAY_BUFFER, model.buffers[i].data.size(), model.buffers[i].data.data(), 0);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return buffers;
}

std::vector<GLuint> ViewerApplication::createVertexArrayObjects(
    const tinygltf::Model &model, const std::vector<GLuint> &bufferObjects, std::vector<VaoRange> &meshindexToVaoRange)
{
  std::vector<GLuint> vertexArrayObjects;

  const GLuint VERTEX_ATTRIB_POSITION_IDX = 0;
  const GLuint VERTEX_ATTRIB_NORMAL_IDX = 1;
  const GLuint VERTEX_ATTRIB_TEXCOORD0_IDX = 2;

  for (auto mesh : model.meshes)
  {
    const auto vaoOffset = vertexArrayObjects.size();
    vertexArrayObjects.resize(vaoOffset + mesh.primitives.size());
    meshindexToVaoRange.push_back(VaoRange{GLsizei(vaoOffset), GLsizei(mesh.primitives.size())});
    glGenVertexArrays(GLsizei(mesh.primitives.size()), &vertexArrayObjects[vaoOffset]);

    for (size_t idx = 0; idx < mesh.primitives.size(); idx++)
    {
      const auto vao = vertexArrayObjects[vaoOffset + idx];
      glBindVertexArray(vao);
      const auto primitive = mesh.primitives[idx];

      {
        const auto iterator = primitive.attributes.find("POSITION");

        if (iterator != end(primitive.attributes))
        {
          const auto accessorIdx = (*iterator).second;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;

          const auto bufferObject = bufferObjects[bufferIdx];

          glEnableVertexAttribArray(VERTEX_ATTRIB_POSITION_IDX);
          assert(GL_ARRAY_BUFFER == bufferView.target);
          glBindBuffer(GL_ARRAY_BUFFER, bufferObject);

          const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;

          glVertexAttribPointer(VERTEX_ATTRIB_POSITION_IDX, accessor.type, accessor.componentType, GL_FALSE, GLsizei(bufferView.byteStride),
              (const GLvoid *)byteOffset);
        }
      }

      {
        const auto iterator = primitive.attributes.find("NORMAL");

        if (iterator != end(primitive.attributes))
        {
          const auto accessorIdx = (*iterator).second;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;

          const auto bufferObject = bufferObjects[bufferIdx];

          glEnableVertexAttribArray(VERTEX_ATTRIB_NORMAL_IDX);
          assert(GL_ARRAY_BUFFER == bufferView.target);
          glBindBuffer(GL_ARRAY_BUFFER, bufferObject);

          const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;

          glVertexAttribPointer(VERTEX_ATTRIB_NORMAL_IDX, accessor.type, accessor.componentType, GL_FALSE, GLsizei(bufferView.byteStride),
              (const GLvoid *)byteOffset);
        }
      }

      {
        const auto iterator = primitive.attributes.find("TEXCOORD_0");

        if (iterator != end(primitive.attributes))
        {
          const auto accessorIdx = (*iterator).second;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;

          const auto bufferObject = bufferObjects[bufferIdx];

          glEnableVertexAttribArray(VERTEX_ATTRIB_NORMAL_IDX);
          assert(GL_ARRAY_BUFFER == bufferView.target);
          glBindBuffer(GL_ARRAY_BUFFER, bufferObject);

          const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;

          glVertexAttribPointer(VERTEX_ATTRIB_TEXCOORD0_IDX, accessor.type, accessor.componentType, GL_FALSE,
              GLsizei(bufferView.byteStride), (const GLvoid *)byteOffset);
        }
      }

      if (primitive.indices >= 0)
      {
        const auto accessorIdx = primitive.indices;
        const auto &accessor = model.accessors[accessorIdx];
        const auto &bufferView = model.bufferViews[accessor.bufferView];
        const auto bufferIdx = bufferView.buffer;

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferObjects[bufferIdx]);
      }
    }
  }
  glBindVertexArray(0);

  return vertexArrayObjects;
}

int ViewerApplication::run()
{
  // Loader shaders
  const auto glslProgram = compileProgram({m_ShadersRootPath / m_vertexShader, m_ShadersRootPath / m_fragmentShader});

  const auto modelViewProjMatrixLocation = glGetUniformLocation(glslProgram.glId(), "uModelViewProjMatrix");
  const auto modelViewMatrixLocation = glGetUniformLocation(glslProgram.glId(), "uModelViewMatrix");
  const auto normalMatrixLocation = glGetUniformLocation(glslProgram.glId(), "uNormalMatrix");

  // Build projection matrix
  auto maxDistance = 500.f; // TODO use scene bounds instead to compute this
  maxDistance = maxDistance > 0.f ? maxDistance : 100.f;
  const auto projMatrix = glm::perspective(70.f, float(m_nWindowWidth) / m_nWindowHeight, 0.001f * maxDistance, 1.5f * maxDistance);

  // TODO Implement a new CameraController model and use it instead. Propose
  // the choice from the GUI
  FirstPersonCameraController cameraController{m_GLFWHandle.window(), 0.5f * maxDistance};
  if (m_hasUserCamera)
  {
    cameraController.setCamera(m_userCamera);
  } else
  {
    // TODO Use scene bounds to compute a better default camera
    cameraController.setCamera(Camera{glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0)});
  }

  tinygltf::Model model;

  loadGltfFile(model);

  auto modelBufferObjects = createBufferObjects(model);

  std::vector<VaoRange> meshindexToVaoRange;
  const auto vertexArrayObjects = createVertexArrayObjects(model, modelBufferObjects, meshindexToVaoRange);

  // Setup OpenGL state for rendering
  glEnable(GL_DEPTH_TEST);
  glslProgram.use();

  // Lambda function to draw the scene
  const auto drawScene = [&](const Camera &camera) {
    glViewport(0, 0, m_nWindowWidth, m_nWindowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto viewMatrix = camera.getViewMatrix();

    // The recursive function that should draw a node
    // We use a std::function because a simple lambda cannot be recursive
    const std::function<void(int, const glm::mat4 &)> drawNode = [&](int nodeIdx, const glm::mat4 &parentMatrix) {
      // TODO The drawNode function
      auto node = model.nodes[nodeIdx];
      glm::mat4 modelMatrix = getLocalToWorldMatrix(node, parentMatrix);
      if (node.mesh >= 0)
      {
        glm::mat4 modelViewMatrix = viewMatrix * modelMatrix;

        glm::mat4 modelViewProjectionMatrix = projMatrix * modelViewMatrix;

        glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelViewMatrix));

        glUniformMatrix4fv(modelViewMatrixLocation, 1, GL_FALSE, glm::value_ptr(modelViewMatrix));
        glUniformMatrix4fv(modelViewProjMatrixLocation, 1, GL_FALSE, glm::value_ptr(modelViewProjectionMatrix));
        glUniformMatrix4fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));

        const auto mesh = model.meshes[node.mesh];
        const auto &vaoRange = meshindexToVaoRange[node.mesh];
        for (int primIdx = 0; primIdx < mesh.primitives.size(); primIdx++)
        {
          const auto vao = vertexArrayObjects[vaoRange.begin + primIdx];
          glBindVertexArray(vao);

          const auto &primitive = mesh.primitives[primIdx];
          if (primitive.indices >= 0)
          {
            const auto &accessor = model.accessors[primitive.indices];
            const auto &bufferView = model.bufferViews[accessor.bufferView];
            const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;
            glDrawElements(primitive.mode, accessor.count, accessor.componentType, (const GLvoid *)byteOffset);
          } else
          {
            const auto accessorIdx = (*begin(primitive.attributes)).second;
            const auto &accessor = model.accessors[accessorIdx];
            glDrawArrays(primitive.mode, 0, GLsizei(accessor.count));
          }
        }
      }
      for (auto child : node.children)
      {
        drawNode(child, modelMatrix);
      }
    };

    // Draw the scene referenced by gltf file
    if (model.defaultScene >= 0)
    {
      for (auto node : model.scenes[model.defaultScene].nodes)
      {
        drawNode(node, glm::mat4(1));
      }
    }
  };

  if (!m_OutputPath.empty())
  {
    std::vector<unsigned char> pixels(m_nWindowHeight * m_nWindowWidth * 3);
    renderToImage(m_nWindowWidth, m_nWindowHeight, 3, pixels.data(), [&]() { drawScene(cameraController.getCamera()); });
    flipImageYAxis(m_nWindowWidth, m_nWindowHeight, 3, pixels.data());
    const auto strPath = m_OutputPath.string();
    stbi_write_png(strPath.c_str(), m_nWindowWidth, m_nWindowHeight, 3, pixels.data(), 0);

    return 0;
  }

  // Loop until the user closes the window
  for (auto iterationCount = 0u; !m_GLFWHandle.shouldClose(); ++iterationCount)
  {
    const auto seconds = glfwGetTime();

    const auto camera = cameraController.getCamera();
    drawScene(camera);

    // GUI code:
    imguiNewFrame();

    {
      ImGui::Begin("GUI");
      ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
      {
        ImGui::Text("eye: %.3f %.3f %.3f", camera.eye().x, camera.eye().y, camera.eye().z);
        ImGui::Text("center: %.3f %.3f %.3f", camera.center().x, camera.center().y, camera.center().z);
        ImGui::Text("up: %.3f %.3f %.3f", camera.up().x, camera.up().y, camera.up().z);

        ImGui::Text("front: %.3f %.3f %.3f", camera.front().x, camera.front().y, camera.front().z);
        ImGui::Text("left: %.3f %.3f %.3f", camera.left().x, camera.left().y, camera.left().z);

        if (ImGui::Button("CLI camera args to clipboard"))
        {
          std::stringstream ss;
          ss << "--lookat " << camera.eye().x << "," << camera.eye().y << "," << camera.eye().z << "," << camera.center().x << ","
             << camera.center().y << "," << camera.center().z << "," << camera.up().x << "," << camera.up().y << "," << camera.up().z;
          const auto str = ss.str();
          glfwSetClipboardString(m_GLFWHandle.window(), str.c_str());
        }
      }
      ImGui::End();
    }

    imguiRenderFrame();

    glfwPollEvents(); // Poll for and process events

    auto ellapsedTime = glfwGetTime() - seconds;
    auto guiHasFocus = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
    if (!guiHasFocus)
    {
      cameraController.update(float(ellapsedTime));
    }

    m_GLFWHandle.swapBuffers(); // Swap front and back buffers
  }

  // TODO clean up allocated GL data

  return 0;
}

ViewerApplication::ViewerApplication(const fs::path &appPath, uint32_t width, uint32_t height, const fs::path &gltfFile,
    const std::vector<float> &lookatArgs, const std::string &vertexShader, const std::string &fragmentShader, const fs::path &output) :
    m_nWindowWidth(width),
    m_nWindowHeight(height),
    m_AppPath{appPath},
    m_AppName{m_AppPath.stem().string()},
    m_ImGuiIniFilename{m_AppName + ".imgui.ini"},
    m_ShadersRootPath{m_AppPath.parent_path() / "shaders"},
    m_gltfFilePath{gltfFile},
    m_OutputPath{output}
{
  if (!lookatArgs.empty())
  {
    m_hasUserCamera = true;
    m_userCamera = Camera{glm::vec3(lookatArgs[0], lookatArgs[1], lookatArgs[2]), glm::vec3(lookatArgs[3], lookatArgs[4], lookatArgs[5]),
        glm::vec3(lookatArgs[6], lookatArgs[7], lookatArgs[8])};
  }

  if (!vertexShader.empty())
  {
    m_vertexShader = vertexShader;
  }

  if (!fragmentShader.empty())
  {
    m_fragmentShader = fragmentShader;
  }

  ImGui::GetIO().IniFilename = m_ImGuiIniFilename.c_str(); // At exit, ImGUI will store its windows
                                                           // positions in this file

  glfwSetKeyCallback(m_GLFWHandle.window(), keyCallback);

  printGLVersion();
}
