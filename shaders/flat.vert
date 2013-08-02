#version 330

in vec3 in_Position;

uniform mat4 mvp;

void main() {
    gl_Position = mvp * vec4(in_Position,1);
}
