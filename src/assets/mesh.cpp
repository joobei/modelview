#pragma warning(disable: 4819)

#include "mesh.h"

pho::Mesh::Mesh(std::vector<glm::vec3> vertixes, std::vector<GLushort> indixes, std::vector<glm::vec2> texcoords,std::string name):
vertices(vertixes),
	indices(indixes),
	texcoords(texcoords),
	selected(false),
	name(name)
{
	modelMatrix = glm::mat4();
	glGenVertexArrays(1,&vaoId);
	glGenBuffers(1,&ibId);
	glGenBuffers(1,&vertexVboId);
	glGenBuffers(1,&texCoordVboId);

	glBindVertexArray(vaoId);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibId);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(GLushort),indices.data(),GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibId);
	glBindBuffer(GL_ARRAY_BUFFER,vertexVboId);
	glBufferData(GL_ARRAY_BUFFER,vertices.size()*3*sizeof(GLfloat),vertices.data(),GL_STATIC_DRAW);
	glVertexAttribPointer(vertexLoc,3,GL_FLOAT,GL_FALSE,0,0);
	glEnableVertexAttribArray(vertexLoc);

	glBindBuffer(GL_ARRAY_BUFFER,texCoordVboId);
	glBufferData(GL_ARRAY_BUFFER,texcoords.size()*2*sizeof(GLfloat),texcoords.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(texCoordLoc,3,GL_FLOAT,GL_FALSE,0,0);
    glEnableVertexAttribArray(texCoordLoc);
	glBindVertexArray(0);
}


inline GLuint pho::Mesh::getVaoId() {
	return vaoId;
}

glm::vec3 pho::Mesh::getPosition() {

	return glm::vec3(modelMatrix[3][0],modelMatrix[3][1],modelMatrix[3][2]);
}
