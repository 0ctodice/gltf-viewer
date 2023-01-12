#version 330

in vec3 vViewSpacePosition;
in vec3 vViewSpaceNormal;
in vec2 vTexCoords;

uniform vec3 lighting_direction;
uniform vec3 lighting_intensity;

out vec3 fColor;

void main()
{
   // Need another normalization because interpolation of vertex attributes does not maintain unit length
   vec3 viewSpaceNormal = normalize(vViewSpaceNormal);
   fColor = vec3(1./3.14)*lighting_intensity*dot(viewSpaceNormal, lighting_direction);
}