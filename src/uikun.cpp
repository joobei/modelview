﻿#pragma warning(disable: 4819)
/*  Photonio Graphics Engine
Copyright (C) 2011 Nicholas Katzakis

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.*/

#include "uikun.h"
#define _USE_MATH_DEFINES
#include <math.h>
#include <boost/math/special_functions/round.hpp>
#include "data.pb.h"
#include "glm/gtc/matrix_transform.hpp"
#include <cmath>
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

using namespace std;

Engine::Engine(GLFWwindow *window):
    mainWindow(window),
    eventQueue(),
    udpwork(ioservice),
    _udpserver(ioservice,&eventQueue,&ioMutex),
    appState(select),
    selectionTechnique(virtualHand),
    inputStarted(false),
    mouseMove(false),
    plane(&sr),
    rotTechnique(screenSpace),
    bump(false)
{
    errorLog.open("error.log",std::ios_base::app);

    tuioClient = new TuioClient(3333);
    tuioClient->addTuioListener(this);
    tuioClient->connect();

    if (!tuioClient->isConnected()) {
        std::cout << "Tuio client connection failed" << std::endl;
    }

    verbose = false;

    doubleClick.start();
    keyboardTimer.start();

    netThread = new boost::thread(boost::bind(&boost::asio::io_service::run, &ioservice));

    objectHit=false;
    sphereHit=false;

    axisChange[0][0] = 0; axisChange[0][1] = 0;  axisChange[0][2] = -1;
    axisChange[1][0] = 0; axisChange[1][1] = 1;  axisChange[1][2] = 0;
    axisChange[2][0] = 1; axisChange[2][1] = 0;  axisChange[2][2] = 0;

    tf = new boost::posix_time::time_facet("%d-%b-%Y %H:%M:%S");

    perspective = 80.0f;

    last_mx = last_my = cur_mx = cur_my = 0;

    consumed = false;
    pointerOpacity = 1.0f;

    //set mouse button callback

    glfwSetMouseButtonCallback(window, &Engine::mouseButtonCallback);
    glfwSetCursorPosCallback(window, &Engine::mouseMoveCallback);

    glfwSetWindowUserPointer(window, this);
#define	SHADOW_MAP_RATIO 1;
}

void Engine::initResources() {
    std::string shaderpath; //this is only needed at init time whereas asssetpath might be needed later

    //find the path where the shaders are stored ("shaderpath" file created by cmake with configure_file)
    if (boost::filesystem::exists("shaderpath")) {
        shaderpath = readTextFile("shaderpath");
        shaderpath = shaderpath.substr(0,shaderpath.size()-1); //cmake puts a newline char
        shaderpath.append("/"); //at the end of the string
    }
    else { std::cout << "Shader PATH NOT EXISTENT" << '\n'; }

    if (boost::filesystem::exists("assetpath")) { //if you're not using cmake put the shaders in the same dir as the binary
        assetpath = readTextFile("assetpath");
        assetpath = assetpath.substr(0,assetpath.size()-1); //cmake puts a newline char
        assetpath.append("/"); //at the end of the string
    }
    else { std::cout << "ASSET PATH NOT EXISTENT" << '\n'; }

    generateShadowFBO();

    //sr.light.position = glm::vec3(50,10,20);
    sr.light.position = glm::vec3(0,50,0);
    //sr.light.direction = glm::vec3(0,0,-1);
    sr.light.direction = glm::vec3(0,-1,0);
    sr.light.color = glm::vec4(1,1,1,1);
    sr.light.viewMatrix = glm::lookAt(sr.light.position,glm::vec3(0,0,-60),glm::vec3(0,0,-1));
//    sr.light.viewMatrix = glm::lookAt(sr.light.position,glm::vec3(0,0,0),glm::vec3(0,0,-1));

    //*************************************************************
    //********************  Load Shaders **************************
    //*************************************************************
    sr.flatShader = pho::Shader(shaderpath+"flat");

    noTextureShader = pho::Shader(shaderpath+"notexture");
    noTextureShader.use();
    noTextureShader["view"] = sr.viewMatrix;
    noTextureShader["light_position"] = glm::vec4(sr.light.position,1);
    noTextureShader["light_diffuse"] = sr.light.color;
    noTextureShader["light_specular"] = vec4(1,1,1,1);

    normalMap = pho::Shader(shaderpath+"bump");
    normalMap.use();
    normalMap["view"] = sr.viewMatrix;
    normalMap["light_position"] = glm::vec4(pointLight.position,1);
    normalMap["light_diffuse"] = pointLight.color;
    normalMap["light_specular"] = vec4(1,1,1,1);

    GLuint t1Location = glGetUniformLocation(normalMap.program, "diffuseTexture");
    GLuint t2Location = glGetUniformLocation(normalMap.program, "normalMap");
    GLuint t3Location = glGetUniformLocation(normalMap.program, "shadowMap");

    glUniform1i(t1Location, 0);
    glUniform1i(t2Location, 1);
    glUniform1i(t3Location, 2);

    //shadow map debug
    singleTexture = pho::Shader(shaderpath+"texader");
    singleTexture.use();
    shadowMapLoc = glGetUniformLocation(singleTexture.program, "shadowMap");
    baseImageLoc = glGetUniformLocation(singleTexture.program, "texturex");

    glUniform1i(baseImageLoc, 0);
    glUniform1i(shadowMapLoc, 2);

    // ********* physics *********
    initPhysics();


    //*************************************************************
    //********************  Load Assets ***************************
    //*************************************************************
    cursor = pho::Asset("cursor.obj", &noTextureShader,&sr, false, 1.0f);
    //sr.dynamicsWorld->removeRigidBody(cursor.rigidBody);
    //sr.dynamicsWorld->addRigidBody(cursor.rigidBody, COL_NOTHING, COL_NOTHING);
    cursor.modelMatrix = glm::translate(glm::mat4(),glm::vec3(0,-10,-20));
    selectedAsset = &cursor; //when app starts we control the cursor

    //floor = pho::Asset("floor.obj", &singleTexture,&sr,true);
    floor = pho::Asset("floor.obj", &normalMap,&sr,false, 1.0f);
    floor.modelMatrix  = glm::translate(glm::mat4(),glm::vec3(0,-20,-60));
    floor.receiveShadow = true;

    plane.setShader(&sr.flatShader);
    plane.modelMatrix = cursor.modelMatrix;
    plane.setScale(15.0f);
    //plane.receiveShadow = true;

    heart = pho::Asset("heart.obj",&normalMap,&sr, true, 1.0f);
    heart.rigidBody->setRestitution(4);
    //heart.modelMatrix = glm::translate(glm::mat4(),glm::vec3(-10,10,-25));
    heart.modelMatrix = glm::translate(glm::mat4(),glm::vec3(0,0,-15));
    heart.receiveShadow = true;
    heart.rigidBody->setUserPointer(&heart);
    sr.dynamicsWorld->addRigidBody(heart.rigidBody);

//    pho::Asset bucket = pho::Asset("bucket.obj",&normalMap,&sr, true, 1.0f);
//    bucket.receiveShadow = true;
//    bucket.rigidBody->setRestitution(0);
//    sr.dynamicsWorld->addRigidBody(bucket.rigidBody);
//    boxes.push_back(bucket);
//    bucket.rigidBody->setUserPointer(&boxes.back());

    glm::vec3 disc = glm::vec3(0.0,0.0,0.0);
    GLuint buffer;

    glGenVertexArrays(1,&pointVao);
    glBindVertexArray(pointVao);

    CALL_GL(glGenBuffers(1, &buffer));
    CALL_GL(glBindBuffer(GL_ARRAY_BUFFER, buffer));
    CALL_GL(glBufferData(GL_ARRAY_BUFFER, sizeof(float)*3, glm::value_ptr(disc), GL_STATIC_DRAW));
    CALL_GL(glEnableVertexAttribArray(vertexLoc));
    CALL_GL(glVertexAttribPointer(vertexLoc, 3, GL_FLOAT, 0, 0, 0));

    //Create the perspective matrix
    sr.projectionMatrix = glm::perspective(glm::radians(perspective), (float)WINDOW_SIZE_X/(float)WINDOW_SIZE_Y,0.1f,1000.0f);

    cameraPosition = vec3(0,-13,-15);
//    sr.viewMatrix = glm::lookAt(cameraPosition,vec3(0,-10.3,-16),vec3(0,1,0)); //slightly looking down
    sr.viewMatrix = glm::lookAt(cameraPosition,vec3(0,-13,-16),vec3(0,1,0));

    glEnable (GL_DEPTH_TEST);
    glEnable (GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);


    InverseProjectionMatrix = glm::inverse(sr.projectionMatrix);
    InverseViewMatrix = glm::inverse(sr.viewMatrix);

    intersectedAsset = &cursor;
}

//checks event queue for events
//and consumes them all
void Engine::checkEvents() {

    checkKeyboard();

    checkUDP();

    if (flicker.inFlick(flickState::translation)) {
        glm::mat4 flickTransform = flicker.dampenAndGiveMatrix(glm::mat3(plane.modelMatrix));
        plane.modelMatrix = flickTransform*plane.modelMatrix;  //translate plane
        pho::locationMatch(selectedAsset->modelMatrix,plane.modelMatrix);  //put cursor in plane's location
//        selectedAsset->clipplaneMatrix =flickTransform * selectedAsset->clipplaneMatrix;
    }

    if (flicker.inFlick(pinchy)) {
        glm::mat4 flickTransform = flicker.dampenAndGivePinchMatrix();
        selectedAsset->rotate(flickTransform);
    }

    if (flicker.inFlick(rotation)){
        glm::vec2 rotation = flicker.dampenAndGiveRotationMatrix();
        selectedAsset->rotate(glm::rotate(glm::radians(rotation.x),vec3(0,1,0)));
        selectedAsset->rotate(glm::rotate(glm::radians(rotation.y),vec3(1,0,0)));
    }

    //Joystick
    if(JoystickPresent) { checkSpaceNavigator();}

    if ((appState == select) && (selectionTechnique == twod)) {
        if (rayTest(touchPoint.x,touchPoint.y,intersectedAsset))
        {
            intersectedAsset->beingIntersected = true;
        }
    }

//    cursor.updateMotionState();

    checkPhysics();

    if(switchOnNextFrame) {
        pho::locationMatch(plane.modelMatrix,cursor.modelMatrix);
        //Put 2d cursor where object was dropped:
        //1st. calculate clip space coordinates
        glm::vec4 newtp = sr.projectionMatrix*sr.viewMatrix*heart.modelMatrix*glm::vec4(0,0,0,1);
        //2nd. convert it in normalized device coordinates by dividing with w
        newtp/=newtp.w;
        touchPoint.x = newtp.x;
        touchPoint.y = newtp.y;
        switchOnNextFrame = false;
    }
}

void Engine::render() {

    shadowMapRender();

    CALL_GL(glClearColor(1.0f,1.0f,1.0f,0.0f));
    CALL_GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));

    floor.draw();
    heart.draw();
//    car.draw();

    for(std::vector<pho::Asset>::size_type i = 0; i != boxes.size(); i++) {
        boxes[i].draw();
    }



    if (appState == select) {
        if (selectionTechnique == virtualHand) {
            cursor.draw();
        }

        if (selectionTechnique == twod)
        {
            sr.flatShader.use();

            //sr.flatShader["mvp"] = sr.projectionMatrix*sr.viewMatrix*glm::mat4();
            sr.flatShader["mvp"] = glm::translate(glm::mat4(),glm::vec3(touchPoint.x,touchPoint.y,-1));
            CALL_GL(glBindVertexArray(pointVao));
            CALL_GL(glPointSize(20));
            sr.flatShader["color"] = glm::vec4(1,0,0,pointerOpacity);
            CALL_GL(glDrawArrays(GL_POINTS,0,1));

            CALL_GL(glDisable(GL_DEPTH_TEST));
            sr.flatShader["color"] = glm::vec4(1,1,1,pointerOpacity);
            CALL_GL(glPointSize(10));
            CALL_GL(glDrawArrays(GL_POINTS,0,1));
            CALL_GL(glEnable(GL_DEPTH_TEST));

            pointerOpacity-=0.1;
        }
    }


    if ((appState == clipping) || (appState == translate) || ((appState == select ) && (selectionTechnique == virtualHand))) {
        plane.draw();
    }

    glfwSwapBuffers(mainWindow);
}



void Engine::mouseButtonClicked(int button, int action, int mods)
{
//    //glfwGetMousePos(&cur_mx,&cur_my);
//    std::cout << "testing testing 123" << std::endl;
//    boost::timer::cpu_times const elapsed_times(doubleClick.elapsed());
//    double difference = elapsed_times.wall-previousTime.wall;

//    if (difference > 150000000) {
//        doubleClickPerformed = true;
//    }

//    previousTime = elapsed_times;


//    if ((button == GLFW_MOUSE_BUTTON_1) && (state == GLFW_PRESS))
//    {


//    }
//    if ((button == GLFW_MOUSE_BUTTON_1) && (state == GLFW_RELEASE))
//    {
//        appState = rotate;
//    }
//    if ((button == GLFW_MOUSE_BUTTON_2) && (state == GLFW_PRESS)) {

//        mouseMove = true;
//        prevMouseExists = true;
//        prevMousePos = glm::vec2(cur_mx,cur_my);

//        appState = translate;

//    }
//    if ((button == GLFW_MOUSE_BUTTON_2) && (state == GLFW_RELEASE)) {

//        mouseMove = false;
//        prevMouseExists = false;

//        appState = rotate;
//    }
}


void Engine::mouseMoved(double x, double y)
{

//    float norm_x = 1.0*x/WINDOW_SIZE_X*2 - 1.0;
//    float norm_y = -(1.0*y/WINDOW_SIZE_Y*2 - 1.0);

//    //glm::vec4 mouse_clip = glm::vec4((float)x * 2 / float(WINDOW_SIZE_X) - 1, 1 - float(y) * 2 / float(WINDOW_SIZE_Y),0,1);
//    glm::vec4 mouse_clip = glm::vec4((float)x * 2 / float(WINDOW_SIZE_X) - 1, 1 - float(y) * 2 / float(WINDOW_SIZE_Y),-1,1);

//    glm::vec4 mouse_world = glm::inverse(sr.viewMatrix) * glm::inverse(sr.projectionMatrix) * mouse_clip;

//    rayOrigin = glm::vec3(sr.viewMatrix[3]);
//    rayDirection = glm::normalize(glm::vec3(mouse_world)-rayOrigin);

//    if (appState == translate) {
//        vec2 MousePt;

//        MousePt.x = x;
//        MousePt.y = y;

//        vec2 difference = MousePt-prevMousePos;
//        difference.x /= 50;
//        difference.y /= 50;
//        difference.y = -difference.y;

//        if(prevMouseExists) {
//            selectedAsset->modelMatrix = glm::translate(glm::vec3(difference,0))*selectedAsset->modelMatrix;
//        }

//        prevMousePos = MousePt;

//    }
}

void Engine::go() {
    initResources();
    while(!glfwWindowShouldClose(mainWindow) && !(glfwGetKey(mainWindow,GLFW_KEY_ESCAPE) == GLFW_PRESS)) {
        glfwPollEvents();
        checkEvents();
        render();

    }
    shutdown();
}

void Engine::shutdown() {
    ioservice.stop();
    errorLog.close();
    //serialioservice.stop();
    //psmove_disconnect(move);
}


void Engine::addTuioCursor(TuioCursor *tcur) {
    inputStarted = true;
    float x,y;
    x = tcur->getX();
    y = tcur->getY();

    //TUIO variables
    std::list<TUIO::TuioCursor*> cursorList;
    cursorList = tuioClient->getTuioCursors();
    short numberOfCursors = cursorList.size();
    //std::cout << "Added cursor, Current NoOfCursors " << numberOfCursors << std::endl;

    //notify flick manager of a new gesture starting
    if (numberOfCursors == 1 && ( appState == translate || appState == select || appState == clipping)) {
        p1c.x=x;
        p1c.y=y;
        p1p=p1c;
        f1id = tcur->getCursorID();
        flicker.newFlick(flickState::translation);
        flicker.stopFlick(flickState::pinchy);
    }
    if (numberOfCursors == 1 && ( appState == rotate)) {
        flicker.stopFlick(flickState::rotation);
    }

    if ((numberOfCursors == 2) && (appState == translate || appState == clipping)) {
        p2c.x = x;
        p2c.y = y;
        f2id = tcur->getCursorID();
        lastFingerDistance = glm::distance(p2c,p1c);
        p2p = p2c ;  //when first putting finger down there must be
    }

    boost::timer::cpu_times const elapsed_times(doubleClick.elapsed());
    double difference = elapsed_times.wall-previousTime.wall;

    if (numberOfCursors == 1 ) {
        if (difference < 170000000) { //doubleclick

            //doubleclick
            switch(appState) {
            case select:
                if (selectionTechnique == virtualHand) {

                    sr.dynamicsWorld->performDiscreteCollisionDetection();
                    int numManifolds = sr.dynamicsWorld->getDispatcher()->getNumManifolds();
                    for (int i=0;i<numManifolds;i++)
                    {
                        btPersistentManifold* contactManifold = sr.dynamicsWorld->getDispatcher()->getManifoldByIndexInternal(i);
                        const btCollisionObject* obA = contactManifold->getBody0();
                        const btCollisionObject* obB = contactManifold->getBody1();

                        if (obA==coCursor && (obA != NULL) && (obB != NULL) && (static_cast<pho::Asset*>(obB->getUserPointer()) != NULL))
                        {
                            static_cast<pho::Asset*>(obB->getUserPointer())->beingIntersected=true;
                            selectedAsset = static_cast<pho::Asset*>(obB->getUserPointer());

                            grabbedVector = glm::vec3(cursor.modelMatrix[3])-glm::vec3(selectedAsset->modelMatrix[3]);
                            plane.modelMatrix = selectedAsset->modelMatrix;
                            appState = translate;

                            sr.dynamicsWorld->removeRigidBody(selectedAsset->rigidBody);
                        }
                        if (obB==coCursor && (obA != NULL) && (obB != NULL) && (static_cast<pho::Asset*>(obA->getUserPointer()) != NULL))
                        {
                            static_cast<pho::Asset*>(obA->getUserPointer())->beingIntersected=true;
                            selectedAsset = static_cast<pho::Asset*>(obA->getUserPointer());

                            grabbedVector = glm::vec3(cursor.modelMatrix[3])-glm::vec3(selectedAsset->modelMatrix[3]);
                            plane.modelMatrix = selectedAsset->modelMatrix;
                            appState = translate;

                            sr.dynamicsWorld->removeRigidBody(selectedAsset->rigidBody);
                        }
                    }

                }
                if (selectionTechnique == twod && rayTest(touchPoint.x,touchPoint.y,intersectedAsset))
                {
                    selectedAsset = intersectedAsset;
                    plane.modelMatrix = selectedAsset->modelMatrix;
                    appState = translate;
                    sr.dynamicsWorld->removeRigidBody(selectedAsset->rigidBody);
                }
                break;
            case translate:
                deselect();
                break;
            }
        }
    }
    previousTime = elapsed_times;


    switch (appState) {
    case select:
        if ((numberOfCursors == 2) && rayTest(touchPoint.x,touchPoint.y,intersectedAsset) && (selectionTechnique ==twod)) {
            appState = translate;
            selectedAsset = intersectedAsset;
            sr.dynamicsWorld->removeRigidBody(selectedAsset->rigidBody);
            pho::locationMatch(plane.modelMatrix,selectedAsset->modelMatrix);
            lastFingerDistance = glm::distance(p2c,p1c);
            break;
        }
        break;
    case translate:
        break;
    case rotate:
        if (numberOfCursors == 0) {
            flicker.stopFlick(rotation);
        }
        if (numberOfCursors == 1) {
            p1c.x = x;
            p1c.y = y;
            f1id = tcur->getCursorID();

            p1p = p1c;  //when first putting finger down there must be
            // a previous spot otherwise when the other finger moves it goes crazy
            p1f = p1c; //save 1st touched point
        }
        if (numberOfCursors == 2) {

            if ((p1f.x > 0.8) && (p1f.y < 0.2))
            {

                rotTechnique = locky;
                //std::cout << "Lock Y axis" << std::endl;
            }
            else if ((p1f.x < 0.2) && (p1f.y > 0.8))
            {

                rotTechnique = lockx;
                //std::cout << "Lock X axis" << std::endl;
            }
            else if ((p1f.x < 0.6) && (p1f.x > 0.4) && (p1f.y > 0.4) && (p1f.y < 0.6) && (x < 0.6) && (x > 0.4) && (y > 0.4) && (y < 0.6))
            {

                rotTechnique = lockz;
                // std::cout << "Lock Z axis" << std::endl;
            }
            else
            {

                rotTechnique = pinch;
                //std::cout << "pinch rotate" << '\n';


                p2p = p2c;   //when first putting finger down there must be
                // a previous spot otherwise when the other finger moves it goes crazy

                consumed = true;
            }

            flicker.stopFlick(pinchy);

            p2c.x = tcur->getX();
            p2c.y = tcur->getY();
            f2id = tcur->getCursorID();

            p2p = p2c;   //when first putting finger down there must be
            // a previous spot otherwise when the other finger moves it goes crazy

            consumed = true;
        }
        break;
    default:
        break;
    }

    if (verbose)
        std::cout << "add cur " << tcur->getCursorID() << " (" <<  tcur->getSessionID() << ") " << tcur->getX() << " " << tcur->getY() << std::endl;
}

void Engine::updateTuioCursor(TuioCursor *tcur) {
    vec3 newLocationVector;
    btTransform temp;
    float x,y;
    x = tcur->getX();
    y = tcur->getY();
    mat3 tempMat;
    mat4 newLocationMatrix;
    mat4 tempMatrix;

    //std::cout << "x: " << x;
    //std::cout << "\t\t y: " << y << std::endl;

    short numberOfCursors = tuioClient->getTuioCursors().size();
    flicker.addTouch(glm::vec2(tcur->getXSpeed(),tcur->getYSpeed()));

    vec2 ft;
    float newAngle;

    switch (appState) {
    case direct:
        break;
    case clipping:
        if (numberOfCursors == 2)
        {
            if (tcur->getCursorID() == f1id) {
                p1c.x = x;
                p1c.y = y;
            }

            if (tcur->getCursorID() == f2id) {
                p2c.x = x;
                p2c.y = y;
            }
            float currentFingerDistance = glm::distance(p2c,p1c);
            float distanceTravelled = lastFingerDistance-currentFingerDistance;

            tempMat = mat3(orientation);  //get the rotation part from the plane's matrix
            newLocationVector = tempMat*vec3(0,distanceTravelled*10,0); //on the normal to the plane

            if (distanceTravelled < 0.2)
            {
                newLocationMatrix = glm::translate(mat4(),newLocationVector);   //Calculate new location by translating object by motion vector
                plane.modelMatrix = newLocationMatrix*plane.modelMatrix;
//                pho::locationMatch(selectedAsset->modelMatrix,plane.modelMatrix);
                pho::locationMatch(selectedAsset->clipplaneMatrix,plane.modelMatrix);  //put clipping plane in plane's location
            }

            lastFingerDistance = currentFingerDistance;
        }
        else
        {
            tempMat = mat3(orientation);  //get the rotation part from the plane's matrix
#define TFACTORA 5
            x=(tcur->getXSpeed())/TFACTORA;
            y=(tcur->getYSpeed())/TFACTORA;
            //add cursor to queue for flicking

            newLocationVector = tempMat*vec3(x,0,y);  //rotate the motion vector from TUIO in the direction of the plane
            newLocationMatrix = glm::translate(mat4(),newLocationVector);   //Calculate new location by translating object by motion vector

            plane.modelMatrix = newLocationMatrix*plane.modelMatrix;  //translate plane
            pho::locationMatch(selectedAsset->clipplaneMatrix,plane.modelMatrix);  //put clipping plane in plane's location
        }
        break;
    case select:
        if (selectionTechnique == twod) {
            touchPoint.x += tcur->getXSpeed()/30;
            touchPoint.y += -1*tcur->getYSpeed()/30;
            break;
        }
    case translate:
        //********************* TRANSLATE ****************************

        if (numberOfCursors == 2)
        {
            if (tcur->getCursorID() == f1id) {
                p1c.x = x;
                p1c.y = y;
            }

            if (tcur->getCursorID() == f2id) {
                p2c.x = x;
                p2c.y = y;
            }
            float currentFingerDistance = glm::distance(p2c,p1c);
            float distanceTravelled = lastFingerDistance-currentFingerDistance;

            tempMat = mat3(orientation);  //get the rotation part from the plane's matrix
            newLocationVector = tempMat*vec3(0,-1*distanceTravelled*10,0); //on the normal to the plane

            if (distanceTravelled < 0.2)
            {
                newLocationMatrix = glm::translate(mat4(),newLocationVector);   //Calculate new location by translating object by motion vector
                plane.modelMatrix = newLocationMatrix*plane.modelMatrix;
                pho::locationMatch(selectedAsset->modelMatrix,plane.modelMatrix);
            }

            lastFingerDistance = currentFingerDistance;
        }
        else
        {

            tempMat = mat3(orientation);  //get the rotation part from the plane's matrix
            x=(tcur->getXSpeed())/TFACTORA;
            y=(tcur->getYSpeed())/TFACTORA;
            //add cursor to queue for flicking

            newLocationVector = tempMat*vec3(x,0,y);  //rotate the motion vector from TUIO in the direction of the plane
            newLocationMatrix = glm::translate(mat4(),newLocationVector);   //Calculate new location by translating object by motion vector

            plane.modelMatrix = newLocationMatrix*plane.modelMatrix;  //translate plane
            pho::locationMatch(selectedAsset->modelMatrix,plane.modelMatrix);  //put cursor in plane's location
            if (selectedAsset->clipped)
            {
                pho::displace(selectedAsset->clipplaneMatrix,newLocationVector);
            }
        }
        break;
        //*********************   ROTATE  ****************************
    case rotate:
        switch (rotTechnique) {
        case clutch:
            break;
        case screenSpace:
            if (flicker.inFlick(rotation)) { flicker.stopFlick(rotation); } //probably have come back from a pinch flick so need to stop the flick?? test without.

            if (tcur->getCursorID() == f1id) {

                p1p = p1c;
                selectedAsset->rotate(glm::rotate(glm::radians(tcur->getXSpeed()*2),vec3(0,1,0)));
                selectedAsset->rotate(glm::rotate(glm::radians(tcur->getYSpeed()*2),vec3(1,0,0)));

                p1c.x = tcur->getX();
                p1c.y = tcur->getY();

                flicker.addTouch(glm::vec2(tcur->getXSpeed(),tcur->getYSpeed()));
            }

            if (tcur->getCursorID() == f2id) {

                p2p = p2c;
                selectedAsset->rotate(glm::rotate(glm::radians(tcur->getXSpeed()),vec3(0,1,0)));
                selectedAsset->rotate(glm::rotate(glm::radians(tcur->getYSpeed()),vec3(1,0,0)));

                p2c.x = tcur->getX();
                p2c.y = tcur->getY();

            }
            break;
        case lockz:
        case pinch:
            // ***  PINCH  **********

            if (tcur->getCursorID() == f1id) {

                p1p = p1c;

                p1c.x = tcur->getX();
                p1c.y = tcur->getY();

                consumed = true;

                flicker.addTouch(glm::vec2(tcur->getXSpeed(),tcur->getYSpeed()));
            }

            if (tcur->getCursorID() == f2id) {

                p2p = p2c;

                p2c.x = tcur->getX();
                p2c.y = tcur->getY();

                consumed = false;
            }

            if (consumed == false) {

                p1t = p1c-p1p;
                p2t = p2c-p2p;

                ft.x=std::max(0.0f,std::min(p1t.x,p2t.x)) + std::min(0.0f,std::max(p1t.x,p2t.x));
                ft.y=std::max(0.0f,std::min(p1t.y,p2t.y)) + std::min(0.0f,std::max(p1t.y,p1t.y));

                referenceAngle = atan2((p2p.y - p1p.y) ,(p2p.x - p1p.x));
                newAngle = atan2((p2c.y - p1c.y),(p2c.x - p1c.x));

                selectedAsset->rotate(glm::rotate(glm::radians((newAngle-referenceAngle)*(-50)),vec3(0,0,1)));

                selectedAsset->rotate(glm::rotate(glm::radians(ft.x*50),vec3(0,1,0)));
                selectedAsset->rotate(glm::rotate(glm::radians(ft.y*50),vec3(1,0,0)));

                flicker.addRotate(newAngle-referenceAngle);

                consumed = true;
            }

            break;
        case locky:
            if (tcur->getCursorID() == f2id) {

                p2p = p2c;
                selectedAsset->rotate(glm::rotate(glm::radians(tcur->getXSpeed()*2.0f),vec3(0,1,0)));
                p2c.x = tcur->getX();
                p2c.y = tcur->getY();
            }
            break;
        case lockx:
            if (tcur->getCursorID() == f2id) {

                p2p = p2c;
                //selectedAsset->rotate(glm::rotate(tcur->getXSpeed()*3.0f,vec3(0,1,0)));
                selectedAsset->rotate(glm::rotate(glm::radians(tcur->getYSpeed()*2.0f),vec3(1,0,0)));

                p2c.x = tcur->getX();
                p2c.y = tcur->getY();
            }
            break;
        }
    }
    if (verbose)
        std::cout << "set cur " << tcur->getCursorID() << " (" <<  tcur->getSessionID() << ") " << tcur->getX() << " " << tcur->getY()
                  << " " << tcur->getMotionSpeed() << " " << tcur->getMotionAccel() << " " << std::endl;

}

void Engine::removeTuioCursor(TuioCursor *tcur) {

    switch (appState) {
    case select:
        if (selectionTechnique != virtualHand)
            break;
    case translate:
        flicker.endFlick(glm::mat3(orientation),translation);
        break;
    case rotate:
        switch (rotTechnique) {
        case clutch:
            break;
        case screenSpace:
            flicker.endFlick(glm::mat3(orientation),rotation);
            break;
        case lockx:
        case locky:
        case lockz:
        case pinch:
            rotTechnique = screenSpace;
            //std::cout << "screenSpace" << std::endl;
            flicker.endFlick(glm::mat3(orientation),pinchy);
            break;
        }
        break;
    default:
        break;
    }

    if (verbose)
        std::cout << "del cur " << tcur->getCursorID() << " (" <<  tcur->getSessionID() << ")" << std::endl;
}

btVector3 Engine::getRayTo(glm::vec2 xy)
{
    //btVector3	DemoApplication::getRayTo(int x,int y)

    float top = 1.f;
    float bottom = -1.f;
    float nearPlane = 1.f;
    float tanFov = (top-bottom)*0.5f / nearPlane;
    float fov = btScalar(2.0) * btAtan(tanFov);

    btVector3 rayFrom = btVector3(xy.x,xy.y,0);
    glm::vec3 rf = glm::mat3(sr.viewMatrix)*glm::vec3(0,0,-1);
    btVector3 rayForward = btVector3(rf.x,rf.y,rf.z);

    rayForward.normalize();
    float farPlane = -10000.f;
    rayForward*= farPlane;

    btVector3 rightOffset;
    btVector3 vertical = btVector3(0,1,0);

    btVector3 hor;
    hor = rayForward.cross(vertical);
    hor.normalize();
    vertical = hor.cross(rayForward);
    vertical.normalize();

    float tanfov = tanf(0.5f*fov);


    hor *= 2.f * farPlane * tanfov;
    vertical *= 2.f * farPlane * tanfov;

    btScalar aspect;

    aspect = WINDOW_SIZE_X / (btScalar)WINDOW_SIZE_Y;

    hor*=aspect;


    btVector3 rayToCenter = rayFrom + rayForward;
    btVector3 dHor = hor * 1.f/float(WINDOW_SIZE_X);
    btVector3 dVert = vertical * 1.f/float(WINDOW_SIZE_Y);


    btVector3 rayTo = rayToCenter - 0.5f * hor + 0.5f * vertical;
    rayTo += btScalar(xy.x) * dHor;
    rayTo -= btScalar(xy.y) * dVert;
    return rayTo;
}

void Engine::refresh(TuioTime frameTime) {
    //std::cout << "refresh " << frameTime.getTotalMilliseconds() << std::endl;
}

void Engine::checkUDP() {
    glm::quat Q; //to hold the quaternion generated from the location vector
    glm::mat3 temp; //to hold converted rotation matrix
    //UDP queue

    //    boost::mutex::scoped_lock lock(ioMutex);

    while (!eventQueue.isEmpty()) {
        //assign event to local temp var
        keimote::PhoneEvent tempEvent;
        tempEvent = eventQueue.pop();

        //check event to see what type it is
        switch (tempEvent.type()) {
        case keimote::ROTATION:
            rotationVector.x = tempEvent.x();
            rotationVector.y = tempEvent.y();
            rotationVector.z = tempEvent.z();

            Q[0] = 1 - rotationVector[0]*rotationVector[0] - rotationVector[1]*rotationVector[1] - rotationVector[2]*rotationVector[2];
            Q[0] = (Q[0] > 0) ? (float)glm::sqrt(Q[0]) : 0;


            Q[1] = rotationVector[0]; Q[2] = rotationVector[2]; Q[3] = rotationVector[1]; //NEXUS 5

            temp = glm::mat3_cast(Q);
            orientation = glm::mat4(temp*axisChange);

            orientation = calibration*orientation;

            //apply to plane
            plane.modelMatrix[0][0] = orientation[0][0]; plane.modelMatrix[0][1] = orientation[0][1]; plane.modelMatrix[0][2] = orientation[0][2];
            plane.modelMatrix[1][0] = orientation[1][0]; plane.modelMatrix[1][1] = orientation[1][1]; plane.modelMatrix[1][2] = orientation[1][2];
            plane.modelMatrix[2][0] = orientation[2][0]; plane.modelMatrix[2][1] = orientation[2][1]; plane.modelMatrix[2][2] = orientation[2][2];

            if (appState == clipping) { //apply to clipping plane too
                selectedAsset->clipplaneMatrix[0][0] = orientation[0][0]; selectedAsset->clipplaneMatrix[0][1] = orientation[0][1]; selectedAsset->clipplaneMatrix[0][2] = orientation[0][2];
                selectedAsset->clipplaneMatrix[1][0] = orientation[1][0]; selectedAsset->clipplaneMatrix[1][1] = orientation[1][1]; selectedAsset->clipplaneMatrix[1][2] = orientation[1][2];
                selectedAsset->clipplaneMatrix[2][0] = orientation[2][0]; selectedAsset->clipplaneMatrix[2][1] = orientation[2][1]; selectedAsset->clipplaneMatrix[2][2] = orientation[2][2];
            }

            break;
        case keimote::BUTTON:
            switch (tempEvent.buttontype()) {
            case 1:
                calibration = glm::inverse(orientation);
                break;
            case 2:
                if (tempEvent.state() == true && appState == translate) {
                    appState = rotate;
                    rotTechnique = screenSpace;
                    printf("translate --> rotate (Screenspace)");
                }
                if (tempEvent.state() == true && appState == clipping) {
                    locationMatch(plane.modelMatrix,selectedAsset->modelMatrix);
                    appState = rotate;
                    rotTechnique = screenSpace;
                    printf("translate --> rotate (Screenspace)");
                }
                if (tempEvent.state() == false && appState == rotate) {
                    appState = translate;
                    lastFingerDistance = glm::distance(p2c,p1c); //fix jumping bug when
                    //releasing rotation button
                }

                break;
            case 3:
                if (tempEvent.state() && (appState == select)) {
                    sr.dynamicsWorld->removeCollisionObject(coCursor);
                    coCursor->setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
                    sr.dynamicsWorld->addCollisionObject(coCursor);
                    std::cout << "Kinematic Object" << std::endl;
                }
                if (!tempEvent.state() && (appState == select)) {
                    sr.dynamicsWorld->removeCollisionObject(coCursor);
                    coCursor->setCollisionFlags(coCursor->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);
                    sr.dynamicsWorld->addCollisionObject(coCursor);

                    std::cout << "No contact response" << std::endl;
                }
                if (tempEvent.state() && appState == rotate) {
                    appState = direct;
                    rotTechnique = clutch;
                    printf("translate --> rotate (Direct)");
                }
                if (tempEvent.state() == false && appState == rotate) {
                    appState = translate;
                }

                break;
            case 4:
                if (appState == clipping) {
                    appState = translate;
                }
                else {
                    if (!selectedAsset->clipped) {
                        selectedAsset->clipplaneMatrix = selectedAsset->modelMatrix;
                        selectedAsset->clipped = true;
                        plane.modelMatrix = selectedAsset->clipplaneMatrix;
                        appState = clipping;
                    }
                    else
                    {
                        selectedAsset->clipped = false;
                        plane.modelMatrix = selectedAsset->modelMatrix;
                        appState = translate;
                    }

                }
                break;
            default:
                break;
            }
            break;
        };
    }
}

void Engine::checkKeyboard() {
#define FACTOR 0.5f
    //static bool started = false;
    boost::timer::cpu_times const elapsed_times(keyboardTimer.elapsed());
    double difference = elapsed_times.wall-keyboardPreviousTime.wall;

    if (difference > 800000000) { keyPressOK = true;}
    //to stop multiple keypresses
    if (keyPressOK) {
        if (glfwGetKey(mainWindow, GLFW_KEY_DOWN)) {
            sr.viewMatrix = glm::translate(sr.viewMatrix, vec3(0,0,-FACTOR));
        }

        if (glfwGetKey(mainWindow,GLFW_KEY_UP)) {
            sr.viewMatrix = glm::translate(sr.viewMatrix, vec3(0,0,FACTOR));
        }
        if (glfwGetKey(mainWindow,GLFW_KEY_LEFT)) {
            sr.viewMatrix = glm::translate(sr.viewMatrix, vec3(FACTOR,0,0));
        }
        if (glfwGetKey(mainWindow,GLFW_KEY_RIGHT)) {
            sr.viewMatrix = glm::translate(sr.viewMatrix, vec3(-FACTOR,0,0));
        }
        if (glfwGetKey(mainWindow,GLFW_KEY_PAGE_UP)) {
            sr.viewMatrix = glm::translate(sr.viewMatrix, vec3(0,-FACTOR,0));
        }
        if (glfwGetKey(mainWindow,GLFW_KEY_PAGE_DOWN)) {
            sr.viewMatrix = glm::translate(sr.viewMatrix, vec3(0,FACTOR,0));
        }
        if (glfwGetKey(mainWindow,GLFW_KEY_HOME)) {
            perspective +=1.0;
            sr.projectionMatrix = glm::perspective(perspective, (float)WINDOW_SIZE_X/(float)WINDOW_SIZE_Y,0.1f,1000.0f);
            std::cout << "Perspective : " << perspective << '\n';

        }
        if (glfwGetKey(mainWindow,GLFW_KEY_SPACE)) {
            calibration = glm::inverse(orientation);
            keyPressOK = false;
            keyboardPreviousTime =  elapsed_times;
        }
        if (glfwGetKey(mainWindow,'9')) {
            selectedAsset=&cursor;
            selectedAsset->modelMatrix = glm::translate(glm::vec3(0,-10,-15));
            plane.modelMatrix = cursor.modelMatrix;
            flicker.stopFlick(translation);
            flicker.stopFlick(rotation);
            flicker.stopFlick(pinchy);
            sr.viewMatrix = glm::lookAt(cameraPosition,vec3(0,-10,-1),vec3(0,1,0));
            appState = select;
            touchPoint.x=0.0f;
            touchPoint.y=0.0f;
            keyPressOK = false;
            keyboardPreviousTime =  elapsed_times;
        }

        if (glfwGetKey(mainWindow,'5') == GLFW_PRESS) {
            appState = select;
        }
        if (glfwGetKey(mainWindow,GLFW_KEY_END)) {
            perspective -=1.0;
            sr.projectionMatrix = glm::perspective(perspective, (float)WINDOW_SIZE_X/(float)WINDOW_SIZE_Y,0.1f,1000.0f);
            //log("Perspective : " +perspective);
        }

        if (glfwGetKey(mainWindow,'1') == GLFW_PRESS) {
            locationMatch(cursor.modelMatrix,heart.modelMatrix);
            locationMatch(plane.modelMatrix,cursor.modelMatrix);
            selectedAsset = &cursor;
            appState = select;
            selectionTechnique = virtualHand;
            rotTechnique = screenSpace;
            log("virtualHand");
            keyPressOK = false;
            keyboardPreviousTime =  elapsed_times;
            sr.dynamicsWorld->addCollisionObject(coCursor);
        }

        if (glfwGetKey(mainWindow,'2') == GLFW_PRESS) {
            appState = select;
            selectionTechnique = twod;
            selectedAsset = &cursor;
            log("2D Selection");
            keyPressOK = false;
            keyboardPreviousTime =  elapsed_times;
            sr.dynamicsWorld->removeCollisionObject(coCursor);
        }

        if (glfwGetKey(mainWindow,'3') == GLFW_PRESS) {
            clipDistance +=1;
            keyPressOK = false;
            keyboardPreviousTime =  elapsed_times;

        }
        if (glfwGetKey(mainWindow,'4') == GLFW_PRESS) {
            clipDistance -=1;
            keyPressOK = false;
            keyboardPreviousTime =  elapsed_times;

        }

        if(glfwGetKey(mainWindow,'0') == GLFW_PRESS) {
            for(int i = 0;i<15;++i) {
                pho::Asset newBox = pho::Asset("box.obj",&normalMap,&sr, true, 1.0f);
                newBox.receiveShadow = true;
                sr.dynamicsWorld->addRigidBody(newBox.rigidBody);
                boxes.push_back(newBox);
                newBox.rigidBody->setUserPointer(&boxes.back());
            }
            keyPressOK = false;
            keyboardPreviousTime =  elapsed_times;
        }

        if(glfwGetKey(mainWindow,'9') == GLFW_PRESS) {
//            keyPressOK = false;
//            keyboardPreviousTime =  elapsed_times;
        }

    }
}

void Engine::generateShadowFBO()
{
    int shadowMapWidth = WINDOW_SIZE_X * (int)SHADOW_MAP_RATIO;
    int shadowMapHeight =  WINDOW_SIZE_Y * (int)SHADOW_MAP_RATIO;

    CALL_GL(glGenTextures(1, &(sr.shadowTexture)));
    CALL_GL(glActiveTexture(GL_TEXTURE0));
    CALL_GL(glBindTexture(GL_TEXTURE_2D, sr.shadowTexture));
    CALL_GL(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, shadowMapWidth, shadowMapHeight, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, 0));
    CALL_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    CALL_GL(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    CALL_GL(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    CALL_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE));
    CALL_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS));
    CALL_GL(glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,shadowMapWidth,shadowMapHeight,0,GL_DEPTH_COMPONENT,GL_FLOAT,NULL));
    CALL_GL(glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE));
    CALL_GL(glBindTexture(GL_TEXTURE_2D, 0)); //unbind the texture

    CALL_GL(glGenFramebuffers(1, &shadowFBO));
    CALL_GL(glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO));
    CALL_GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sr.shadowTexture, 0));
    CALL_GL(glDrawBuffer(GL_NONE));
    CALL_GL(glReadBuffer(GL_NONE));

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    { printf("GL_FRAMEBUFFER_COMPLETE error 0x%x", glCheckFramebufferStatus(GL_FRAMEBUFFER)); }

    CALL_GL(glClearDepth(1.0f); glEnable(GL_DEPTH_TEST));
    // Needed when rendering the shadow map. This will avoid artifacts.
    CALL_GL(glPolygonOffset(1.0f, 0.0f); glBindFramebuffer(GL_FRAMEBUFFER, 0));
    //to convert the texture coordinates to -1 ~ 1
    GLfloat biasMatrixf[] = {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.5f, 0.5f, 0.5f, 1.0f };

    sr.biasMatrix = glm::make_mat4(biasMatrixf);
}

void Engine::shadowMapRender() {
    int shadowMapWidth = WINDOW_SIZE_X * (int)SHADOW_MAP_RATIO;
    int shadowMapHeight =  WINDOW_SIZE_Y * (int)SHADOW_MAP_RATIO;

    // Rendering into the shadow texture.
    CALL_GL(glActiveTexture(GL_TEXTURE0));
    CALL_GL(glBindTexture(GL_TEXTURE_2D, sr.shadowTexture));
    // Bind the framebuffer.
    CALL_GL(glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO));
    //Clear it
    CALL_GL(glClear(GL_DEPTH_BUFFER_BIT));
    CALL_GL(glViewport(0, 0, shadowMapWidth, shadowMapHeight));
    CALL_GL(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE));

    CALL_GL(glEnable(GL_CULL_FACE));
    CALL_GL(glCullFace(GL_BACK));

    if ((appState == select) && (selectionTechnique == virtualHand)) cursor.drawFromLight();
    heart.drawFromLight();

    for(std::vector<pho::Asset>::size_type i = 0; i != boxes.size(); i++) {
        boxes[i].drawFromLight();
    }

    CALL_GL(glDisable(GL_CULL_FACE));
    // Revert for the scene.
    CALL_GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    CALL_GL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
    CALL_GL(glViewport(0, 0, WINDOW_SIZE_X, WINDOW_SIZE_Y));

}

void Engine::checkSpaceNavigator() { 

#define TRSCALE 2.0f
#define RTSCALE 0.5f
    unsigned char buttons[2];
    int arraySize{};
    auto const position(glfwGetJoystickAxes(GLFW_JOYSTICK_1,&arraySize));

    if (glfwGetJoystickButtons(GLFW_JOYSTICK_1,&arraySize)[0] == GLFW_PRESS)
    {
        navmode = false;
        std::cout << "Asset" << std::endl;
    }
    if (glfwGetJoystickButtons(GLFW_JOYSTICK_1,&arraySize)[1] == GLFW_PRESS)
    {
        navmode =true;
        std::cout << "View" << std::endl;
    }


    if (navmode)
    {

        sr.viewMatrix = glm::translate(vec3(-1*position[0]*TRSCALE,0,0))*sr.viewMatrix;
        sr.viewMatrix = glm::translate(vec3(0,position[2]*TRSCALE,0))*sr.viewMatrix;
        sr.viewMatrix = glm::translate(vec3(0,0,-1*position[1]*TRSCALE))*sr.viewMatrix;
        sr.viewMatrix = glm::rotate(RTSCALE*position[5],glm::vec3(0,1,0))*sr.viewMatrix;
        sr.viewMatrix = glm::rotate(RTSCALE*-1*position[4],glm::vec3(0,0,1))*sr.viewMatrix;
        sr.viewMatrix = glm::rotate(RTSCALE*-1*position[3],glm::vec3(1,0,0))*sr.viewMatrix;
    }
    else
    {
        selectedAsset->modelMatrix = glm::translate(vec3(position[0]*TRSCALE,0,0))*selectedAsset->modelMatrix;
        selectedAsset->modelMatrix = glm::translate(vec3(0,-1*position[2]*TRSCALE,0))*selectedAsset->modelMatrix;
        selectedAsset->modelMatrix = glm::translate(vec3(0,0,position[1]*TRSCALE))*selectedAsset->modelMatrix;
        selectedAsset->rotate(glm::rotate(RTSCALE*-1*position[5],glm::vec3(0,1,0)));
        selectedAsset->rotate(glm::rotate(RTSCALE*position[4],glm::vec3(0,0,1)));
        selectedAsset->rotate(glm::rotate(RTSCALE*position[3],glm::vec3(1,0,0)));
    }


}

void Engine::initPhysics()
{
    // Build the broadphase
    btBroadphaseInterface* broadphase = new btDbvtBroadphase();

    // Set up the collision configuration and dispatcher
    btDefaultCollisionConfiguration* collisionConfiguration = new btDefaultCollisionConfiguration();
    btCollisionDispatcher* dispatcher = new btCollisionDispatcher(collisionConfiguration);

    // The actual physics solver
    btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver;

    // The world.
    sr.dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher,broadphase,solver,collisionConfiguration);
    sr.dynamicsWorld->setGravity(btVector3(0,-10,0));

    btCollisionShape* groundShape = new btStaticPlaneShape(btVector3(0,1,0),-10);
    btDefaultMotionState* groundMotionState = new btDefaultMotionState(btTransform(btQuaternion(0,0,0,1),btVector3(0,-10,0)));

    btRigidBody::btRigidBodyConstructionInfo
            groundRigidBodyCI(0,groundMotionState,groundShape,btVector3(0,-10,0));
    btRigidBody* groundRigidBody1 = new btRigidBody(groundRigidBodyCI);

    sr.dynamicsWorld->addRigidBody(groundRigidBody1,collisiontypes::COL_EVERYTHING,collisiontypes::COL_EVERYTHING);

    // **************************************************************************************************
    groundShape = new btStaticPlaneShape(btVector3(0,0,1),-10);
    groundMotionState = new btDefaultMotionState(btTransform(btQuaternion(0,0,0,1),btVector3(0,0,-48)));


    groundRigidBodyCI = btRigidBody::btRigidBodyConstructionInfo(0,groundMotionState,groundShape,btVector3(0,0,-48));
    btRigidBody* groundRigidBody2 = new btRigidBody(groundRigidBodyCI);

    sr.dynamicsWorld->addRigidBody(groundRigidBody2,collisiontypes::COL_EVERYTHING,collisiontypes::COL_EVERYTHING);

    // **************************************************************************************************
    btTransform temp;

    btSphereShape* csSphere = new btSphereShape(1.0f);
    coCursor = new btGhostObject();
    temp.setFromOpenGLMatrix(glm::value_ptr(cursor.modelMatrix));

    coCursor->setCollisionShape(csSphere);
    coCursor->setWorldTransform(temp);
    coCursor->setUserPointer(&cursor);
    coCursor->setActivationState(DISABLE_DEACTIVATION);
    coCursor->setCollisionFlags(coCursor->getCollisionFlags() | btCollisionObject::CF_NO_CONTACT_RESPONSE);

    sr.dynamicsWorld->addCollisionObject(coCursor);

}

glm::mat4 Engine::convertBulletTransformToGLM(const btTransform& transform)
{
    float data[16];
    transform.getOpenGLMatrix(data);
    return glm::make_mat4(data);
}


void Engine::checkPhysics()
{

    sr.dynamicsWorld->stepSimulation(1/30.f,10);  //default 1/30

    if ((appState == select ) && (selectionTechnique == virtualHand)) {

        btTransform temp;
        temp.setFromOpenGLMatrix(glm::value_ptr(cursor.modelMatrix));
        coCursor->setWorldTransform(temp);

        sr.dynamicsWorld->performDiscreteCollisionDetection();
        int numManifolds = sr.dynamicsWorld->getDispatcher()->getNumManifolds();
        for (int i=0;i<numManifolds;i++)
        {
            btPersistentManifold* contactManifold = sr.dynamicsWorld->getDispatcher()->getManifoldByIndexInternal(i);
            const btCollisionObject* obA = contactManifold->getBody0();
            const btCollisionObject* obB = contactManifold->getBody1();

            if (obA==coCursor && (obA != NULL) && (obB != NULL) && (static_cast<pho::Asset*>(obB->getUserPointer()) != NULL)) {
                static_cast<pho::Asset*>(obB->getUserPointer())->beingIntersected=true;
            }
            if (obB==coCursor && (obA != NULL) && (obB != NULL) && (static_cast<pho::Asset*>(obA->getUserPointer()) != NULL)) {
                static_cast<pho::Asset*>(obA->getUserPointer())->beingIntersected=true;
            }
        }
    }

    btTransform trans;

    if (selectedAsset != &heart)
    {
        heart.rigidBody->getMotionState()->getWorldTransform(trans);
        heart.modelMatrix = convertBulletTransformToGLM(trans);
    }

    for(std::vector<pho::Asset>::size_type i = 0; i != boxes.size(); i++)
    {
        if (selectedAsset != &boxes[i])
        {
            boxes[i].rigidBody->getMotionState()->getWorldTransform(trans);
            boxes[i].modelMatrix = convertBulletTransformToGLM(trans);
        }
    }
}



bool Engine::rayTest(const float &normalizedX, const float &normalizedY, pho::Asset*& intersected) {

    // The ray Start and End positions, in Normalized Device Coordinates (Have you read Tutorial 4 ?)
    glm::vec4 lRayStart_NDC(
                normalizedX,
                normalizedY,
                -1.0, // The near plane maps to Z=-1 in Normalized Device Coordinates
                1.0f
                );
    glm::vec4 lRayEnd_NDC(
                normalizedX,
                normalizedY,
                0.0,
                1.0f
                );


    // The Projection matrix goes from Camera Space to NDC.
    // So inverse(ProjectionMatrix) goes from NDC to Camera Space.
    glm::mat4 InverseProjectionMatrix = glm::inverse(sr.projectionMatrix);

    // The View Matrix goes from World Space to Camera Space.
    // So inverse(ViewMatrix) goes from Camera Space to World Space.
    glm::mat4 InverseViewMatrix = glm::inverse(sr.viewMatrix);

    glm::vec4 lRayStart_camera = InverseProjectionMatrix * lRayStart_NDC;    lRayStart_camera/=lRayStart_camera.w;
    glm::vec4 lRayStart_world  = InverseViewMatrix       * lRayStart_camera; lRayStart_world /=lRayStart_world .w;
    glm::vec4 lRayEnd_camera   = InverseProjectionMatrix * lRayEnd_NDC;      lRayEnd_camera  /=lRayEnd_camera  .w;
    glm::vec4 lRayEnd_world    = InverseViewMatrix       * lRayEnd_camera;   lRayEnd_world   /=lRayEnd_world   .w;


    glm::vec3 lRayDir_world(lRayEnd_world - lRayStart_world);
    lRayDir_world = glm::normalize(lRayDir_world);

    return rayTestWorld(glm::vec3(lRayStart_world),lRayDir_world,intersected);

}

bool Engine::rayTestWorld(const glm::vec3 &origin,const glm::vec3 &direction, pho::Asset*& intersected) {

    glm::vec3 out_origin = origin;
    glm::vec3 out_direction = direction;

    out_direction = out_direction*1000.0f;

    btDynamicsWorld::ClosestRayResultCallback RayCallback(btVector3(out_origin.x, out_origin.y, out_origin.z), btVector3(out_direction.x, out_direction.y, out_direction.z));
    sr.dynamicsWorld->rayTest(btVector3(out_origin.x, out_origin.y, out_origin.z), btVector3(out_direction.x, out_direction.y, out_direction.z), RayCallback);

    if (RayCallback.hasHit()) {
        // !!!!!!!!!!!!!!!!!following line does not give pointer !!!!!!!!!!!!!!!!!
        intersected = static_cast<pho::Asset*>(RayCallback.m_collisionObject->getUserPointer());
        if (intersected != NULL) { return true;}
        else {return false; }
    }
    else {return false;}
}


bool Engine::checkPolhemus(glm::mat4 &modelMatrix) {

    boost::mutex::scoped_lock lock(ioMutex);
    //SERIAL Queue
    while(!eventQueue.isSerialEmpty()) {
        boost::array<float,7> temp = eventQueue.serialPop();

        vec3 position;
        glm::quat orientation;
        mat4 transform;

        //position = vec3(temp[0]/100,temp[1]/100,temp[2]/100);
        position = vec3(temp[0]/100,temp[1]/100,temp[2]/100);
        position += vec3(-0.32,-0.25f,-0.5f);

        orientation.w = temp[3];
        orientation.x = temp[4];
        orientation.y = temp[5];
        orientation.z = temp[6];

        //transform=glm::translate(transform,position);

        transform=glm::toMat4(orientation);

        //Rotate the matrix from the Polhemus tracker so that we can mount it on the wii-mote with the cable running towards the floor.
        transform = transform*glm::toMat4(glm::angleAxis(-90.0f,vec3(0,0,1)));  //multiply from the right --- WRONG!!!!!! but works for the time being

        transform[3][0] = position.x; //add position to the matrix (raw, unrotated)
        transform[3][1] = position.y;
        transform[3][2] = position.z;

        modelMatrix = transform;

        rayOrigin = position;
        rayDirection = glm::mat3(transform)*glm::vec3(0,0,-1);

        return true;
    }

    lock.unlock();
    return false;
}

void Engine::selectAsset(Asset asset)
{

}

void Engine::deselect()
{
    btTransform temp;
    temp.setFromOpenGLMatrix(glm::value_ptr(selectedAsset->modelMatrix));
    //    vec3 pos = vec3(selectedAsset->modelMatrix[3]);
    //    glm::quat Q = glm::quat_cast(selectedAsset->modelMatrix);
    btDefaultMotionState* motionState = new btDefaultMotionState(temp);

    if (selectionTechnique == virtualHand)
    {
        sr.dynamicsWorld->addRigidBody(selectedAsset->rigidBody);
        selectedAsset->rigidBody->setMotionState(motionState);
        cursor.modelMatrix[3] = glm::vec4(glm::vec3(selectedAsset->modelMatrix[3])/*+grabbedVector*/,1);
        selectedAsset = &cursor;
        pho::locationMatch(plane.modelMatrix,cursor.modelMatrix);
    }

    if (selectionTechnique == twod)
    {
        temp.setFromOpenGLMatrix(glm::value_ptr(selectedAsset->modelMatrix));
        selectedAsset->rigidBody->setWorldTransform(temp);
        sr.dynamicsWorld->addRigidBody(selectedAsset->rigidBody);
        //                  locationMatch(cursor.modelMatrix,heart.modelMatrix);
        //                  locationMatch(plane.modelMatrix,cursor.modelMatrix);
        //                  pho::locationMatch(plane.modelMatrix,cursor.modelMatrix);

        //Put 2d cursor where object was dropped:
        //1st. calculate clip space coordinates
        glm::vec4 newtp = sr.projectionMatrix*sr.viewMatrix*selectedAsset->modelMatrix*glm::vec4(0,0,0,1);
        //2nd. convert it in normalized device coordinates by dividing with w
        newtp/=newtp.w;
        touchPoint.x = newtp.x;
        touchPoint.y = newtp.y;
        selectedAsset = &cursor;
    }
    appState = select;
}


void Engine::addTuioObject(TuioObject *tobj) {
    if (verbose)
        std::cout << "add obj " << tobj->getSymbolID() << " (" << tobj->getSessionID() << ") "<< tobj->getX() << " " << tobj->getY() << " " << tobj->getAngle() << std::endl;
}

void Engine::updateTuioObject(TuioObject *tobj) {

    if (verbose)
        std::cout << "set obj " << tobj->getSymbolID() << " (" << tobj->getSessionID() << ") "<< tobj->getX() << " " << tobj->getY() << " " << tobj->getAngle()
                  << " " << tobj->getMotionSpeed() << " " << tobj->getRotationSpeed() << " " << tobj->getMotionAccel() << " " << tobj->getRotationAccel() << std::endl;

}

void Engine::removeTuioObject(TuioObject *tobj) {

    if (verbose)
        std::cout << "del obj " << tobj->getSymbolID() << " (" << tobj->getSessionID() << ")" << std::endl;
}

void Engine::mouseButtonCallback(GLFWwindow *win, int a, int b, int c)
{
    static_cast<pho::Engine*>(glfwGetWindowUserPointer(win))->mouseButtonClicked(a,b,c);
}

void Engine::mouseMoveCallback(GLFWwindow *win, double a, double b)
{
    static_cast<pho::Engine*>(glfwGetWindowUserPointer(win))->mouseMoved(a,b);
}
