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

std::vector<GLuint> ViewerApplication::createBufferObjects(const tinygltf::Model &model) const
{
  std::vector<GLuint> buffers(model.buffers.size(), 0);
  glGenBuffers(GLsizei(model.buffers.size()), buffers.data());
  for (size_t i = 0; i < model.buffers.size(); i++)
  {
    glBindBuffer(GL_ARRAY_BUFFER, buffers[i]);
    glBufferStorage(GL_ARRAY_BUFFER, model.buffers[i].data.size(), model.buffers[i].data.data(), 0);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return buffers;
}

std::vector<GLuint> ViewerApplication::createVertexArrayObjects(
    const tinygltf::Model &model, const std::vector<GLuint> &bufferObjects, std::vector<VaoRange> &meshindexToVaoRange) const
{
  std::vector<GLuint> vertexArrayObjects;

  meshindexToVaoRange.resize(model.meshes.size());

  const GLuint VERTEX_ATTRIB_ATANGENT_IDX = 3;
  const GLuint VERTEX_ATTRIB_ABITANGENT_IDX = 4;

  for (size_t i = 0; i < model.meshes.size(); i++)
  {
    const auto &mesh = model.meshes[i];

    auto &vaoRange = meshindexToVaoRange[i];

    vaoRange.begin = GLsizei(vertexArrayObjects.size());

    vaoRange.count = GLsizei(mesh.primitives.size());

    vertexArrayObjects.resize(vertexArrayObjects.size() + mesh.primitives.size());

    glGenVertexArrays(vaoRange.count, &vertexArrayObjects[vaoRange.begin]);

    for (size_t idx = 0; idx < mesh.primitives.size(); idx++)
    {
      const auto vao = vertexArrayObjects[vaoRange.begin + idx];
      const auto &primitive = mesh.primitives[idx];
      glBindVertexArray(vao);

      std::vector<glm::vec3> tangents;
      std::vector<glm::vec3> bitangents;

      std::string attrs[] = {"POSITION", "NORMAL", "TEXCOORD_0"};

      for (GLuint attrIdx = 0; attrIdx < 3; attrIdx++)
      {
        const auto iterator = primitive.attributes.find(attrs[attrIdx]);

        if (iterator != end(primitive.attributes))
        {
          const auto accessorIdx = (*iterator).second;
          const auto &accessor = model.accessors[accessorIdx];
          const auto &bufferView = model.bufferViews[accessor.bufferView];
          const auto bufferIdx = bufferView.buffer;

          glEnableVertexAttribArray(attrIdx);
          assert(GL_ARRAY_BUFFER == bufferView.target);
          glBindBuffer(GL_ARRAY_BUFFER, bufferObjects[bufferIdx]);

          const auto byteOffset = accessor.byteOffset + bufferView.byteOffset;

          glVertexAttribPointer(
              attrIdx, accessor.type, accessor.componentType, GL_FALSE, GLsizei(bufferView.byteStride), (const GLvoid *)byteOffset);
        }
      }

      {
        const auto &primitive = mesh.primitives[idx];

        const auto positionAttrIdxIt = primitive.attributes.find("POSITION");
        const auto texCoordAttrIdxIt = primitive.attributes.find("TEXCOORD_0");

        if (positionAttrIdxIt == end(primitive.attributes) || texCoordAttrIdxIt == end(primitive.attributes))
        {
          continue;
        }

        const auto &positionAccessor = model.accessors[(*positionAttrIdxIt).second];
        const auto &texCoordAccessor = model.accessors[(*texCoordAttrIdxIt).second];

        const auto &positionBufferView = model.bufferViews[positionAccessor.bufferView];
        const auto &texCoordBufferView = model.bufferViews[texCoordAccessor.bufferView];

        const auto positionByteOffset = positionAccessor.byteOffset + positionBufferView.byteOffset;
        const auto texCoordByteOffset = texCoordAccessor.byteOffset + texCoordBufferView.byteOffset;

        const auto &positionBuffer = model.buffers[positionBufferView.buffer];
        const auto &texCoordBuffer = model.buffers[texCoordBufferView.buffer];

        const auto positionByteStride = positionBufferView.byteStride ? positionBufferView.byteStride : 3 * sizeof(float);
        const auto texCoordByteStride = texCoordBufferView.byteStride ? texCoordBufferView.byteStride : 2 * sizeof(float);

        const auto computeTangentBitangent = [&](uint32_t indexes[]) {
          const glm::vec3 &v0 = *((const glm::vec3 *)&positionBuffer.data[positionByteOffset + positionByteStride * indexes[0]]);
          const glm::vec3 &v1 = *((const glm::vec3 *)&positionBuffer.data[positionByteOffset + positionByteStride * indexes[1]]);
          const glm::vec3 &v2 = *((const glm::vec3 *)&positionBuffer.data[positionByteOffset + positionByteStride * indexes[2]]);

          // Shortcuts for UVs
          const glm::vec2 &uv0 = *((const glm::vec2 *)&texCoordBuffer.data[texCoordByteOffset + texCoordByteStride * indexes[0]]);
          const glm::vec2 &uv1 = *((const glm::vec2 *)&texCoordBuffer.data[texCoordByteOffset + texCoordByteStride * indexes[1]]);
          const glm::vec2 &uv2 = *((const glm::vec2 *)&texCoordBuffer.data[texCoordByteOffset + texCoordByteStride * indexes[2]]);

          // Edges of the triangle : position delta
          glm::vec3 edge1 = v1 - v0;
          glm::vec3 edge2 = v2 - v0;

          // UV delta
          glm::vec2 deltaUV1 = uv1 - uv0;
          glm::vec2 deltaUV2 = uv2 - uv0;

          glm::vec3 tangent;
          glm::vec3 bitangent;

          float factor = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

          float f = 1.0f / factor;

          tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
          tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
          tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

          bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
          bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
          bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);

          tangents.push_back(tangent);
          tangents.push_back(tangent);
          tangents.push_back(tangent);

          bitangents.push_back(bitangent);
          bitangents.push_back(bitangent);
          bitangents.push_back(bitangent);
        };

        if (primitive.indices >= 0)
        {
          const auto &indexAccessor = model.accessors[primitive.indices];
          const auto &indexBufferView = model.bufferViews[indexAccessor.bufferView];
          const auto indexByteOffset = indexAccessor.byteOffset + indexBufferView.byteOffset;
          const auto &indexBuffer = model.buffers[indexBufferView.buffer];
          auto indexByteStride = indexBufferView.byteStride;

          switch (indexAccessor.componentType)
          {
          default:
            std::cerr << "Primitive index accessor with bad componentType " << indexAccessor.componentType << ", skipping it." << std::endl;
            continue;
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            indexByteStride = indexByteStride ? indexByteStride : sizeof(uint8_t);
            break;
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            indexByteStride = indexByteStride ? indexByteStride : sizeof(uint16_t);
            break;
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            indexByteStride = indexByteStride ? indexByteStride : sizeof(uint32_t);
            break;
          }

          uint32_t indexes[3];

          for (size_t j = 0; j < indexAccessor.count; j += 3)
          {
            for (int k = 0; k < 3; k++)
            {
              switch (indexAccessor.componentType)
              {
              case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                indexes[k] = *((const uint8_t *)&indexBuffer.data[indexByteOffset + indexByteStride * (j + k)]);
                break;
              case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                indexes[k] = *((const uint16_t *)&indexBuffer.data[indexByteOffset + indexByteStride * (j + k)]);
                break;
              case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                indexes[k] = *((const uint32_t *)&indexBuffer.data[indexByteOffset + indexByteStride * (j + k)]);
                break;
              }
            }

            computeTangentBitangent(indexes);
          }
        } else
        {
          for (size_t j = 0; j < positionAccessor.count; j += 3)
          {
            uint32_t indexes[3] = {j, j + 1, j + 2};
            computeTangentBitangent(indexes);
          }
        }

        GLuint tangentbuffer;
        glGenBuffers(1, &tangentbuffer);
        glBindBuffer(GL_ARRAY_BUFFER, tangentbuffer);
        glBufferData(GL_ARRAY_BUFFER, tangents.size() * sizeof(glm::vec3), &tangents[0], GL_STATIC_DRAW);

        GLuint bitangentbuffer;
        glGenBuffers(1, &bitangentbuffer);
        glBindBuffer(GL_ARRAY_BUFFER, bitangentbuffer);
        glBufferData(GL_ARRAY_BUFFER, bitangents.size() * sizeof(glm::vec3), &bitangents[0], GL_STATIC_DRAW);

        glEnableVertexAttribArray(VERTEX_ATTRIB_ATANGENT_IDX);
        glBindBuffer(GL_ARRAY_BUFFER, tangentbuffer);
        glVertexAttribPointer(VERTEX_ATTRIB_ATANGENT_IDX, 3, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)0);

        glEnableVertexAttribArray(VERTEX_ATTRIB_ABITANGENT_IDX);
        glBindBuffer(GL_ARRAY_BUFFER, bitangentbuffer);
        glVertexAttribPointer(VERTEX_ATTRIB_ABITANGENT_IDX, 3, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)0);

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
  }
  glBindVertexArray(0);

  return vertexArrayObjects;
}

std::vector<GLuint> ViewerApplication::createTextureObjects(const tinygltf::Model &model) const
{
  std::vector<GLuint> textureObjects(model.textures.size(), 0);

  tinygltf::Sampler defaultSampler;
  defaultSampler.minFilter = GL_LINEAR;
  defaultSampler.magFilter = GL_LINEAR;
  defaultSampler.wrapS = GL_REPEAT;
  defaultSampler.wrapT = GL_REPEAT;
  defaultSampler.wrapR = GL_REPEAT;

  glActiveTexture(GL_TEXTURE0);
  glGenTextures(GLsizei(model.textures.size()), textureObjects.data());

  for (size_t i = 0; i < model.textures.size(); i++)
  {
    const auto &texture = model.textures[i];
    const auto &image = model.images[texture.source];

    const auto &sampler = texture.sampler >= 0 ? model.samplers[texture.sampler] : defaultSampler;

    glBindTexture(GL_TEXTURE_2D, textureObjects[i]);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, image.pixel_type, image.image.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampler.minFilter != -1 ? sampler.minFilter : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampler.magFilter != -1 ? sampler.magFilter : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sampler.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sampler.wrapT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, sampler.wrapR);
    if (sampler.minFilter == GL_NEAREST_MIPMAP_NEAREST || sampler.minFilter == GL_NEAREST_MIPMAP_LINEAR ||
        sampler.minFilter == GL_LINEAR_MIPMAP_NEAREST || sampler.minFilter == GL_LINEAR_MIPMAP_LINEAR)
    {
      glGenerateMipmap(GL_TEXTURE_2D);
    }
  }
  glBindTexture(GL_TEXTURE_2D, 0);

  return textureObjects;
}

int ViewerApplication::run()
{
  // Loader shaders
  const auto glslProgram = compileProgram({m_ShadersRootPath / m_vertexShader, m_ShadersRootPath / m_fragmentShader});

  const auto modelLocation = glGetUniformLocation(glslProgram.glId(), "uModelMatrix");
  const auto modelViewProjMatrixLocation = glGetUniformLocation(glslProgram.glId(), "uModelViewProjMatrix");
  const auto modelViewMatrixLocation = glGetUniformLocation(glslProgram.glId(), "uModelViewMatrix");
  const auto normalMatrixLocation = glGetUniformLocation(glslProgram.glId(), "uNormalMatrix");
  const auto lightingDirectionLocation = glGetUniformLocation(glslProgram.glId(), "uLightDirection");
  const auto lightingIntensityLocation = glGetUniformLocation(glslProgram.glId(), "uLightIntensity");
  const auto uBaseColorTexture = glGetUniformLocation(glslProgram.glId(), "uBaseColorTexture");
  const auto uBaseColorFactor = glGetUniformLocation(glslProgram.glId(), "uBaseColorFactor");
  const auto uMetallicFactor = glGetUniformLocation(glslProgram.glId(), "uMetallicFactor");
  const auto uRoughnessFactor = glGetUniformLocation(glslProgram.glId(), "uRoughnessFactor");
  const auto uMetallicRoughnessTexture = glGetUniformLocation(glslProgram.glId(), "uMetallicRoughnessTexture");
  const auto uEmissiveFactor = glGetUniformLocation(glslProgram.glId(), "uEmissiveFactor");
  const auto uEmissiveTexture = glGetUniformLocation(glslProgram.glId(), "uEmissiveTexture");
  const auto uOcclusionTexture = glGetUniformLocation(glslProgram.glId(), "uOcclusionTexture");
  const auto uOcclusionStrength = glGetUniformLocation(glslProgram.glId(), "uOcclusionStrength");
  const auto uApplyOcclusion = glGetUniformLocation(glslProgram.glId(), "uApplyOcclusion");
  const auto uNormalTexture = glGetUniformLocation(glslProgram.glId(), "uNormalTexture");
  const auto uApplyNormalMapping = glGetUniformLocation(glslProgram.glId(), "uApplyNormalMapping");
  const auto uThereIsANormalMap = glGetUniformLocation(glslProgram.glId(), "uThereIsANormalMap");

  tinygltf::Model model;

  if (!loadGltfFile(model))
  {
    return -1;
  }

  glm::vec3 bboxMin, bboxMax;
  computeSceneBounds(model, bboxMin, bboxMax);

  auto up = glm::vec3(0, 1, 0);
  auto center = (bboxMin + bboxMax) * 0.5f;
  auto diag = bboxMax - bboxMin;

  // Build projection matrix
  auto maxDistance = glm::length(diag);

  maxDistance = maxDistance > 0.f ? maxDistance : 100.f;
  const auto projMatrix = glm::perspective(70.f, float(m_nWindowWidth) / m_nWindowHeight, 0.001f * maxDistance, 1.5f * maxDistance);

  // TODO Implement a new CameraController model and use it instead. Propose
  // the choice from the GUI
  std::unique_ptr<CameraController> cameraController =
      std::make_unique<TrackballCameraController>(m_GLFWHandle.window(), 0.25f * maxDistance);

  if (m_hasUserCamera)
  {
    cameraController->setCamera(m_userCamera);
  } else
  {
    auto eye = diag.z > 0 ? diag : center + 2.f * glm::cross(diag, up);
    cameraController->setCamera(Camera{diag, center, up});
  }

  glm::vec3 lightingDirection(1, 1, 1);
  glm::vec3 lightingIntensity(1, 1, 1);
  bool lightFromCamera = false;
  bool applyOcclusion = true;
  bool applyNormalMapping = true;
  bool thereIsANormalMap = false;

  auto textureObjects = createTextureObjects(model);

  GLuint whiteTexture = 0;

  glGenTextures(1, &whiteTexture);
  glBindTexture(GL_TEXTURE_2D, whiteTexture);
  float white[] = {1, 1, 1, 1};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_FLOAT, white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_REPEAT);
  glBindTexture(GL_TEXTURE_2D, 0);

  auto modelBufferObjects = createBufferObjects(model);

  std::vector<VaoRange> meshindexToVaoRange;
  const auto vertexArrayObjects = createVertexArrayObjects(model, modelBufferObjects, meshindexToVaoRange);

  // Setup OpenGL state for rendering
  glEnable(GL_DEPTH_TEST);
  glslProgram.use();

  const auto bindMaterial = [&](const auto materialIndex) {
    // Material binding
    if (materialIndex >= 0)
    {
      const auto &material = model.materials[materialIndex];
      const auto &pbrMetallicRoughness = material.pbrMetallicRoughness;

      if (uBaseColorFactor >= 0)
      {
        glUniform4f(uBaseColorFactor, (float)pbrMetallicRoughness.baseColorFactor[0], (float)pbrMetallicRoughness.baseColorFactor[1],
            (float)pbrMetallicRoughness.baseColorFactor[2], (float)pbrMetallicRoughness.baseColorFactor[3]);
      }

      if (uMetallicFactor >= 0)
      {
        glUniform1f(uMetallicFactor, (float)pbrMetallicRoughness.metallicFactor);
      }

      if (uRoughnessFactor >= 0)
      {
        glUniform1f(uRoughnessFactor, (float)pbrMetallicRoughness.roughnessFactor);
      }

      if (uEmissiveFactor >= 0)
      {
        glUniform3f(
            uEmissiveFactor, (float)material.emissiveFactor[0], (float)material.emissiveFactor[1], (float)material.emissiveFactor[2]);
      }

      if (uOcclusionStrength >= 0)
      {
        glUniform1f(uOcclusionStrength, (float)material.occlusionTexture.strength);
      }

      if (uBaseColorTexture >= 0)
      {
        auto textureObject = whiteTexture;
        if (pbrMetallicRoughness.baseColorTexture.index >= 0)
        {
          const auto &texture = model.textures[pbrMetallicRoughness.baseColorTexture.index];

          if (texture.source >= 0)
          {
            textureObject = textureObjects[texture.source];
          }
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureObject);
        glUniform1i(uBaseColorTexture, 0);
      }

      if (uMetallicRoughnessTexture >= 0)
      {
        auto metallicRoughnessObject = 0u;
        if (pbrMetallicRoughness.metallicRoughnessTexture.index >= 0)
        {
          const auto &metallicRoughness = model.textures[pbrMetallicRoughness.metallicRoughnessTexture.index];
          if (metallicRoughness.source >= 0)
          {
            metallicRoughnessObject = textureObjects[metallicRoughness.source];
          }
          glActiveTexture(GL_TEXTURE1);
          glBindTexture(GL_TEXTURE_2D, metallicRoughnessObject);
          glUniform1i(uMetallicRoughnessTexture, 1);
        }
      }

      if (uEmissiveTexture >= 0)
      {
        auto emissiveObject = 0u;
        if (material.emissiveTexture.index >= 0)
        {
          const auto &texture = model.textures[material.emissiveTexture.index];
          if (texture.source >= 0)
          {
            emissiveObject = textureObjects[texture.source];
          }
        }

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, emissiveObject);
        glUniform1i(uEmissiveTexture, 2);
      }

      if (uOcclusionTexture >= 0)
      {
        auto occlusionObject = whiteTexture;
        if (material.occlusionTexture.index >= 0)
        {
          const auto &texture = model.textures[material.occlusionTexture.index];
          if (texture.source >= 0)
          {
            occlusionObject = textureObjects[texture.source];
          }
        }
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, occlusionObject);
        glUniform1i(uOcclusionTexture, 3);
      }

      if (uNormalTexture >= 0)
      {
        auto normalObject = 0u;
        if (material.normalTexture.index >= 0)
        {
          const auto &texture = model.textures[material.normalTexture.index];
          if (texture.source >= 0)
          {
            thereIsANormalMap = true;
            normalObject = textureObjects[texture.source];
          }
        }
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, normalObject);
        glUniform1i(uNormalTexture, 4);
      }

    } else
    {
      if (uBaseColorFactor >= 0)
      {
        glUniform4f(uBaseColorFactor, 1, 1, 1, 1);
      }

      if (uMetallicFactor >= 0)
      {
        glUniform1f(uMetallicFactor, 1.f);
      }

      if (uRoughnessFactor >= 0)
      {
        glUniform1f(uRoughnessFactor, 1.f);
      }

      if (uEmissiveFactor >= 0)
      {
        glUniform3f(uEmissiveFactor, 0.f, 0.f, 0.f);
      }

      if (uOcclusionStrength >= 0)
      {
        glUniform1f(uOcclusionStrength, 0.f);
      }

      if (uBaseColorTexture >= 0)
      {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, whiteTexture);
        glUniform1i(uBaseColorTexture, 0);
      }

      if (uMetallicRoughnessTexture >= 0)
      {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(uMetallicRoughnessTexture, 1);
      }

      if (uEmissiveTexture >= 0)
      {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(uEmissiveTexture, 2);
      }

      if (uOcclusionTexture >= 0)
      {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(uOcclusionTexture, 3);
      }

      if (uNormalTexture >= 0)
      {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(uNormalTexture, 4);
      }
    }
  };

  // Lambda function to draw the scene
  const auto drawScene = [&](const Camera &camera) {
    glViewport(0, 0, m_nWindowWidth, m_nWindowHeight);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const auto viewMatrix = camera.getViewMatrix();

    if (lightingDirectionLocation >= 0)
    {
      const auto lightDirectionInViewSpace =
          lightFromCamera ? glm::vec3(0, 0, 1) : glm::normalize(glm::vec3(viewMatrix * glm::vec4(lightingDirection, 0.)));
      glUniform3f(lightingDirectionLocation, lightDirectionInViewSpace[0], lightDirectionInViewSpace[1], lightDirectionInViewSpace[2]);
    }

    if (lightingIntensityLocation >= 0)
    {
      glUniform3f(lightingIntensityLocation, lightingIntensity[0], lightingIntensity[1], lightingIntensity[2]);
    }

    if (uApplyOcclusion >= 0)
    {
      glUniform1i(uApplyOcclusion, applyOcclusion);
    }

    if (uApplyNormalMapping >= 0)
    {
      glUniform1i(uApplyNormalMapping, applyNormalMapping);
    }

    if (uThereIsANormalMap >= 0)
    {
      glUniform1i(uThereIsANormalMap, thereIsANormalMap);
    }

    // The recursive function that should draw a node
    // We use a std::function because a simple lambda cannot be recursive
    const std::function<void(int, const glm::mat4 &)> drawNode = [&](int nodeIdx, const glm::mat4 &parentMatrix) {
      const auto &node = model.nodes[nodeIdx];
      glm::mat4 modelMatrix = getLocalToWorldMatrix(node, parentMatrix);
      if (node.mesh >= 0)
      {
        const glm::mat4 modelViewMatrix = viewMatrix * modelMatrix;

        const glm::mat4 modelViewProjectionMatrix = projMatrix * modelViewMatrix;

        const glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelViewMatrix));

        glUniformMatrix4fv(modelViewMatrixLocation, 1, GL_FALSE, glm::value_ptr(modelViewMatrix));
        glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(modelMatrix));
        glUniformMatrix4fv(modelViewProjMatrixLocation, 1, GL_FALSE, glm::value_ptr(modelViewProjectionMatrix));
        glUniformMatrix4fv(normalMatrixLocation, 1, GL_FALSE, glm::value_ptr(normalMatrix));

        const auto &mesh = model.meshes[node.mesh];
        const auto &vaoRange = meshindexToVaoRange[node.mesh];
        for (int primIdx = 0; primIdx < mesh.primitives.size(); primIdx++)
        {
          const auto vao = vertexArrayObjects[vaoRange.begin + primIdx];

          const auto &primitive = mesh.primitives[primIdx];

          bindMaterial(primitive.material);

          glBindVertexArray(vao);

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
      for (const auto child : node.children)
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
    renderToImage(m_nWindowWidth, m_nWindowHeight, 3, pixels.data(), [&]() { drawScene(cameraController->getCamera()); });
    flipImageYAxis(m_nWindowWidth, m_nWindowHeight, 3, pixels.data());
    const auto strPath = m_OutputPath.string();
    stbi_write_png(strPath.c_str(), m_nWindowWidth, m_nWindowHeight, 3, pixels.data(), 0);

    return 0;
  }

  // Loop until the user closes the window
  for (auto iterationCount = 0u; !m_GLFWHandle.shouldClose(); ++iterationCount)
  {
    const auto seconds = glfwGetTime();

    const auto camera = cameraController->getCamera();
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

        static int cameraControllerType = 0;

        const auto cameraControllerTypeChanged =
            ImGui::RadioButton("Trackball", &cameraControllerType, 0) || ImGui::RadioButton("First Person", &cameraControllerType, 1);

        if (cameraControllerTypeChanged)
        {
          const auto currentCamera = cameraController->getCamera();
          if (cameraControllerType == 0)
          {
            cameraController = std::make_unique<TrackballCameraController>(m_GLFWHandle.window(), 0.25f * maxDistance);
          } else
          {
            cameraController = std::make_unique<FirstPersonCameraController>(m_GLFWHandle.window(), 0.5f * maxDistance);
          }
          cameraController->setCamera(currentCamera);
        }
      }

      if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
      {
        static float theta = 0.0f, phi = 0.0f;
        const auto lightChanged = ImGui::SliderFloat("theta angle", &theta, 0.0f, glm::pi<float>()) ||
                                  ImGui::SliderFloat("phi angle", &phi, 0.0f, 2. * glm::pi<float>());
        if (lightChanged)
        {
          lightingDirection = glm::vec3(sinf(theta) * cosf(phi), cosf(theta), sinf(theta) * sinf(phi));
        }
        static glm::vec3 color{1.0f, 1.0f, 1.0f};
        static float factor = 1.f;
        const auto colorChanged = ImGui::ColorEdit3("light color", (float *)&color) || ImGui::InputFloat("light factor", &factor);
        if (colorChanged)
        {
          lightingIntensity = color * factor;
        }
        ImGui::Checkbox("light from camera", &lightFromCamera);
        ImGui::Checkbox("Ambient occlusion", &applyOcclusion);
        ImGui::Checkbox("Normal Mapping", &applyNormalMapping);
      }

      ImGui::End();
    }

    imguiRenderFrame();

    glfwPollEvents(); // Poll for and process events

    auto ellapsedTime = glfwGetTime() - seconds;
    auto guiHasFocus = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
    if (!guiHasFocus)
    {
      cameraController->update(float(ellapsedTime));
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
