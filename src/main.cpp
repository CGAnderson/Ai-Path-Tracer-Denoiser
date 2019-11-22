#include "main.h"
#include "preview.h"
#include <cstring>
#define TINYOBJLOADER_IMPLEMENTATION // define this in only *one* .cc
#include "tiny_obj_loader.h"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include<filesystem>

#include<Windows.h>
static std::string startTimeString;

// For camera controls
static bool leftMousePressed = false;
static bool rightMousePressed = false;
static bool middleMousePressed = false;
static double lastX;
static double lastY;

static bool camchanged = true;
static float dtheta = 0, dphi = 0;
static glm::vec3 cammove;

float zoom, theta, phi;
glm::vec3 cameraPosition;
glm::vec3 ogLookAt; // for recentering the camera

Scene *scene;
RenderState *renderState;
int iteration;
int width;
int height;
#define SAVE_DENOISE true
//-------------------------------
//-------------MAIN--------------
//-------------------------------
string filename;
int main(int argc, char** argv) {
	
    startTimeString = currentTimeString();

    if (argc < 2) {
        printf("Usage: %s SCENEFILE.txt\n", argv[0]);
        return 1;
    }

    const char *sceneFile = argv[1];
	filename = argv[1];
	// Load scene file
	scene = new Scene(sceneFile);
	//load_obj();
    // Set up camera stuff from loaded path tracer settings
    iteration = 0;
    renderState = &scene->state;
    Camera &cam = renderState->camera;
    width = cam.resolution.x;
    height = cam.resolution.y;
    glm::vec3 view = cam.view;
    glm::vec3 up = cam.up;
    glm::vec3 right = glm::cross(view, up);
    up = glm::cross(right, view);

    cameraPosition = cam.position;

    // compute phi (horizontal) and theta (vertical) relative 3D axis
    // so, (0 0 1) is forward, (0 1 0) is up
    glm::vec3 viewXZ = glm::vec3(view.x, 0.0f, view.z);
    glm::vec3 viewZY = glm::vec3(0.0f, view.y, view.z);
    phi = glm::acos(glm::dot(glm::normalize(viewXZ), glm::vec3(0, 0, -1)));
    theta = glm::acos(glm::dot(glm::normalize(viewZY), glm::vec3(0, 1, 0)));
    ogLookAt = cam.lookAt;
    zoom = glm::length(cam.position - ogLookAt);

    // Initialize CUDA and GL components
    init();

    // GLFW main loop
    mainLoop();

    return 0;
}

void saveImage() {
    float samples = iteration;
    // output image file
    image img_rgb(width, height);
	image img_normal(width, height);
	image img_albedo(width, height);
	image img_depth(width, height);

    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            int index = x + (y * width);
			img_rgb.setPixel(width - 1 - x, y, glm::vec3(renderState->image[index]) / samples);
			if (samples == 1) {
				img_normal.setPixel(width - 1 - x, y, glm::vec3(glm::abs(renderState->normals[index]) * 100.f));
				img_albedo.setPixel(width - 1 - x, y, glm::vec3(renderState->albedos[index] * 255.f));
				img_depth.setPixel(width - 1 - x, y, glm::vec3(renderState->depth[index] * 10.f, 0, 0));
			}
        }
    }
	// Write to disk
	std::string last_element(filename.substr(filename.rfind("/") + 1));
	cout << last_element << endl;
	last_element = last_element.substr(0, last_element.length() - 4);
	string OutputFolder = "Training_data/" + last_element + "/";
	string rgb_path = "Training_data/" + last_element +"/"+to_string(iteration) + "rgb";
	img_rgb.savePNG_scaled(rgb_path);
	if (samples == 1) {
			string depth_path = "Training_data/" + last_element + "/depth";
			string normal_path = "Training_data/" + last_element + "/normals";
			string albedo_path = "Training_data/" + last_element + "/albedo";
			img_normal.savePNG(normal_path);
			img_albedo.savePNG(albedo_path);
			img_depth.savePNG(depth_path);
		}
	
}

void viewDenoiseRaw(int iter) {
	cv::Mat img_normals(width, height, CV_32FC3);
	cv::Mat img_depth(width, height, CV_32FC3);
	cv::Mat img_albedo(width, height, CV_32FC3);
	cv::Mat img_rgb(width, height, CV_32FC3);
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			int index = x + (y * width);
			// Normals
			glm::vec3 dev_normal = renderState->normals[index];
			cv::Vec3f &normals = img_normals.at<cv::Vec3f>(x, y);
			normals[0] = dev_normal.x;
			normals[1] = dev_normal.y;
			normals[2] = dev_normal.z;
			// Depth
			float dev_depth = renderState->depth[index];
			cv::Vec3f &depth = img_depth.at<cv::Vec3f>(x, y);
			depth[0] = dev_depth;
			depth[1] = 0;
			depth[2] = 0;
			// Albedo
			glm::vec3 dev_albedo = renderState->albedos[index];
			cv::Vec3f &albedos = img_albedo.at<cv::Vec3f>(x, y);
			albedos[0] = dev_albedo.x;
			albedos[1] = dev_albedo.y;
			albedos[2] = dev_albedo.z;
			// RGB
			glm::vec3 dev_rgb = renderState->image[index];
			cv::Vec3f &rgb = img_rgb.at<cv::Vec3f>(x, y);
			rgb[0] = dev_rgb.x / iter;
			rgb[1] = dev_rgb.y / iter;
			rgb[2] = dev_rgb.z / iter;
		}
	}
	// Rotate stuff
	cv::rotate(img_normals, img_normals, cv::ROTATE_90_CLOCKWISE);
	cv::rotate(img_depth, img_depth, cv::ROTATE_90_CLOCKWISE);
	cv::rotate(img_albedo, img_albedo, cv::ROTATE_90_CLOCKWISE);
	cv::rotate(img_rgb, img_rgb, cv::ROTATE_90_CLOCKWISE);
	// Save
	//string depth_path = "../Training_data/Scene_1/" + to_string(iter) + "depth" + string(".png");
	//cv::imwrite(depth_path.c_str(), img_depth);
	//string normal_path = "../Training_data/Scene_1/" + to_string(iter) + "normals" + string(".png");
	//cv::imwrite(normal_path.c_str(), img_normals);
	//string albedo_path = "../Training_data/Scene_1/" + to_string(iter) + "albedo" + string(".png");
	//cv::imwrite(albedo_path.c_str(), img_albedo);
	//string rgb_path = "../Training_data/Scene_1/" + to_string(iter) + "rgb" + string(".png");
	//cv::imwrite(rgb_path.c_str(), img_rgb);
	//cv::waitKey(0);
}
int runCuda() {
    if (camchanged) {
        iteration = 0;
        Camera &cam = renderState->camera;
        cameraPosition.x = zoom * sin(phi) * sin(theta);
        cameraPosition.y = zoom * cos(theta);
        cameraPosition.z = zoom * cos(phi) * sin(theta);

        cam.view = -glm::normalize(cameraPosition);
        glm::vec3 v = cam.view;
        glm::vec3 u = glm::vec3(0, 1, 0);//glm::normalize(cam.up);
        glm::vec3 r = glm::cross(v, u);
        cam.up = glm::cross(r, v);
        cam.right = r;

        cam.position = cameraPosition;
        cameraPosition += cam.lookAt;
        cam.position = cameraPosition;
        camchanged = false;
      }

    // Map OpenGL buffer object for writing from CUDA on a single GPU
    // No data is moved (Win & Linux). When mapped to CUDA, OpenGL should not use this buffer

    if (iteration == 0) {
        pathtraceFree();
        pathtraceInit(scene);
    }
    if (iteration < renderState->iterations) {
        uchar4 *pbo_dptr = NULL;
        iteration++;
        cudaGLMapBufferObject((void**)&pbo_dptr, pbo);

        // execute the kernel
        int frame = 0;
        pathtrace(pbo_dptr, frame, iteration);

        // unmap buffer object
        cudaGLUnmapBufferObject(pbo);
    } else {
		saveImage();
        pathtraceFree();
        cudaDeviceReset();
		return 1;
        //exit(EXIT_SUCCESS);
    }
	if (SAVE_DENOISE)
		saveImage();
	return 0;
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
      switch (key) {
      case GLFW_KEY_ESCAPE:
        saveImage();
        glfwSetWindowShouldClose(window, GL_TRUE);
        break;
      case GLFW_KEY_S:
        saveImage();
        break;
      case GLFW_KEY_SPACE:
        /*camchanged = true;
        renderState = &scene->state;
        Camera &cam = renderState->camera;
        cam.lookAt = ogLookAt;*/
        break;
      }
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
  leftMousePressed = (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS);
  rightMousePressed = (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS);
  middleMousePressed = (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS);
}

void mousePositionCallback(GLFWwindow* window, double xpos, double ypos) {
  if (xpos == lastX || ypos == lastY) return; // otherwise, clicking back into window causes re-start
  if (leftMousePressed) {
    // compute new camera parameters
    phi -= (xpos - lastX) / width;
    theta -= (ypos - lastY) / height;
    theta = std::fmax(0.001f, std::fmin(theta, PI));
    camchanged = true;
  }
  else if (rightMousePressed) {
    zoom += (ypos - lastY) / height;
    zoom = std::fmax(0.1f, zoom);
    camchanged = true;
  }
  else if (middleMousePressed) {
    renderState = &scene->state;
    Camera &cam = renderState->camera;
    glm::vec3 forward = cam.view;
    forward.y = 0.0f;
    forward = glm::normalize(forward);
    glm::vec3 right = cam.right;
    right.y = 0.0f;
    right = glm::normalize(right);

    cam.lookAt -= (float) (xpos - lastX) * right * 0.01f;
    cam.lookAt += (float) (ypos - lastY) * forward * 0.01f;
    camchanged = true;
  }
  lastX = xpos;
  lastY = ypos;
}

///Example code
//-----------------------------------------------------------------------------------------------------
void load_obj() {
	std::string inputfile = "../scenes/bunny.obj";
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	std::string warn;
	std::string err;

	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, inputfile.c_str());

	if (!warn.empty()) {
		std::cout << warn << std::endl;
	}

	if (!err.empty()) {
		std::cerr << err << std::endl;
	}

	if (!ret) {
		exit(1);
	}

	// Loop over shapes
	for (size_t s = 0; s < shapes.size(); s++) {
		// Loop over faces(polygon)
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
			int fv = shapes[s].mesh.num_face_vertices[f];

			// Loop over vertices in the face.
			for (size_t v = 0; v < fv; v++) {
				// access to vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
				tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
				tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
				tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
				tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
				tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
				tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];
				//tinyobj::real_t tx = attrib.texcoords[2 * idx.texcoord_index + 0];
				//tinyobj::real_t ty = attrib.texcoords[2 * idx.texcoord_index + 1];
				// Optional: vertex colors
				// tinyobj::real_t red = attrib.colors[3*idx.vertex_index+0];
				// tinyobj::real_t green = attrib.colors[3*idx.vertex_index+1];
				// tinyobj::real_t blue = attrib.colors[3*idx.vertex_index+2];
			}
			index_offset += fv;

			// per-face material
			shapes[s].mesh.material_ids[f];
		}
	}
}