#ifndef SCENE_H_
#define SCENE_H_

#include "common.h"

struct Scene
{
	vector<SGRTMesh*> meshes;
	Camera *cam;
	RTCScene sgScene;
	int imgWidth;
	int imgHeight;
	vector<Tile> tiles;
	double progress;
	unsigned char *pixels;

	Scene(RTCDevice rtcDevice) {
		srand(time(0));
		sgScene = rtcDeviceNewScene(rtcDevice, RTC_SCENE_STATIC, RTC_INTERSECT1);
		if(!sgScene) {
			printf("Error: Failed initializing RTCScene\n");
		}
	}

	~Scene() {
		delete cam;
		for (int i = 0; i < meshes.size(); i++)
			if(meshes[i])
				delete meshes[i];
		meshes.clear();
		rtcDeleteScene(sgScene);
	}

	class ThreadWorker : public ThreadPoolWorker {
	private:
		int index;
		Scene *scene;
	public:
		ThreadWorker(int i_, Scene* s_) : index(i_), scene(s_) { }

		void operator()() {
			fprintf(stderr, "\rRendering (%d spp) %5.2f%%", randomSamples, 100. * index / (scene->tiles.size() - 1));
			scene->renderTile(scene->tiles[index]);
		}
	};

	void render() {
		progress = 0;
		pixels = (unsigned char*)malloc(imgWidth * imgHeight * 4 * sizeof(unsigned char));

		for (int y = 0; y < imgHeight; y += TILE_SIZE) {
			for (int x = 0; x < imgWidth; x += TILE_SIZE) {
				double midx = imgWidth/2.0, midy = imgHeight/2.0;

				Tile t;
				t.x = x;
				t.y = y;
				t.width = TILE_SIZE;
				t.height = TILE_SIZE;
				t.distance = sqrt((x-midx)*(x-midx) + (y-midy)*(y-midy));

				if(t.x + t.width > imgWidth)
					t.width = imgWidth - t.x;

				if(t.y + t.height > imgHeight)
					t.height = imgHeight - t.y;

				tiles.push_back(t);
			}
		}
		sort(tiles.begin(), tiles.end(), sortByDisance);
		ThreadPool pool = ThreadPool(num_cores());
		for (int i = 0; i < tiles.size(); ++i) {
			pool.enqueueWork(new ThreadWorker(i, this));
		}
		pool.waitEnd();
		fprintf(stderr, "\n");
	}

	void SaveToFile(const char* imagePath, ImageFormat imgFormat) {
		if(imgFormat == ImageFormat_PPM) {
			FILE* file = fopen(imagePath, "wb");
			if (!file) 
				printf("cannot open file\n");

			fprintf(file,"P6\n%i %i\n255\n", imgWidth, imgHeight);

			for (size_t y = 0; y < imgHeight; y++) {
				for (size_t x = 0; x < imgWidth; x++) {
					int pi = ((y * imgWidth) + x) * 4;
					fputc((pixels[pi + 0]), file);
					fputc((pixels[pi + 1]), file);
					fputc((pixels[pi + 2]), file);
				}
			}
			fclose(file);
		} else if(imgFormat == ImageFormat_PNG) {
			unsigned error = lodepng_encode32_file(imagePath, pixels, imgWidth, imgHeight);
			if(error) 
				printf("error %u: %s\n", error, lodepng_error_text(error));
		}
		free(pixels);
	}

	void renderTile(Tile t) {
 		for (int y = t.y; y < t.y + t.height; y++)
			for (int x = t.x; x < t.x + t.width; x++)
				renderPixel(x, y);
	}
	
	void renderPixel(int x, int y) {
		Vec3fa color = Vec3fa(0.0f);

		if(randomSamples > 0) {
			for (int i = 0; i < randomSamples; ++i) {
				double r1 = 2 * GetRandomValue();
				double r2 = 2 * GetRandomValue();
				double dx = r1 < 1 ? sqrt(r1)-1 : 1-sqrt(2-r1);
				double dy = r2 < 1 ? sqrt(r2)-1 : 1-sqrt(2-r2);
				Vec3fa dir = cam->getRayDirection((x + dx)/(double)imgWidth, (y + dy)/(double)imgHeight);
				color = color + getRadiance(cam->position, dir, 0);
			}
			color = color / (double)randomSamples;
		} else {
			Vec3fa dir = cam->getRayDirection(x/(double)imgWidth, y/(double)imgHeight);
			color = color + getRadiance(cam->position, dir, 0);
		}
	
		double ao = 1.0;
		if(samplesAO > 0) {
			Vec3fa dir = cam->getRayDirection(x/(double)imgWidth, y/(double)imgHeight);
			ao = getAmbientOcclusion(cam->position, dir);
		}

		int pi = (((imgHeight - y - 1) * imgWidth) + x) * 4;
		pixels[pi + 0] = (255.0f * clip(color.x, 0.0f, 1.0f) * ao);
		pixels[pi + 1] = (255.0f * clip(color.y, 0.0f, 1.0f) * ao);
		pixels[pi + 2] = (255.0f * clip(color.z, 0.0f, 1.0f) * ao);
		pixels[pi + 3] = 255;
		progress = (y * imgWidth + x) / (1.0f * imgWidth * imgHeight);
	}

	double getAmbientOcclusion(Vec3fa point, Vec3fa dir) {
		RTCRay ray = getIntersection(sgScene, point, dir);

		if (ray.geomID != RTC_INVALID_GEOMETRY_ID && (int)ray.geomID < (int)meshes.size()) {
			Vec3fa n = meshes[ray.geomID]->getInterpolatedNormal(ray.primID, ray.u, ray.v);
			n = n.normalize();
			Vec3fa hitPoint = getHitPoint(ray);
			if(!meshes[ray.geomID]->material.hasLighting)
				return 1.0;

			int hitCount = 0;
			for (int i = 0; i < samplesAO; ++i) {
				Vec3fa nd = sampleAroundNormal(n);
				RTCRay ray = getOcclusion(sgScene, hitPoint, nd, 7.5f);
				if(ray.geomID == 0)
					hitCount++;
			}

			/*
			int hitCount = 0;
			for (int i = 0; i < samplesAO; i+=4) {
				RTCRay4 ray4;
				memset(&ray4,0,sizeof(ray4));
				for (int j = 0; j < 4; i++) {
					Vec3fa nd = sampleAroundNormal(n);
					ray4.orgx[j] = hitPoint.x;
					ray4.orgy[j] = hitPoint.y;
					ray4.orgz[j] = hitPoint.z;

				    ray4.dirx[j] = nd.x;
				    ray4.diry[j] = nd.y;
				    ray4.dirz[j] = nd.z;

				    ray4.tnear[j] = 0.001f;
				    ray4.tfar[j] = 7.5f;
				    ray4.geomID[j] = RTC_INVALID_GEOMETRY_ID;
				    ray4.primID[j] = RTC_INVALID_GEOMETRY_ID;
				    ray4.mask[j] = -1;
				    ray4.time[j] = 0;
				}

				__attribute ((aligned(16))) int valid[4] = { -1,-1,-1,-1 };
			    rtcOccluded4(valid, sgScene, ray4);

				for (int j = 0; j < 4; i++)
					if(ray4.geomID[j] == 0)
						hitCount++;
			}
			*/

			double ambience = 1.0f - (hitCount/(double)samplesAO);
			return (minAOBrightness + ambience * (1.0f - minAOBrightness));
		}

		return 0;
	}

	Vec3fa getRadiance(Vec3fa point, Vec3fa dir, int depth, int E = 1) {
		RTCRay ray = getIntersection(sgScene, point, dir, 0xFFFF0000);
		Vec3fa color = Vec3fa(0.0f);

		if (ray.geomID != RTC_INVALID_GEOMETRY_ID && (int)ray.geomID < (int)meshes.size()) {
			Vec3fa uv = meshes[ray.geomID]->getInterpolatedUV(ray.primID, ray.u, ray.v);
			Vec3fa faceColor = meshes[ray.geomID]->getColor(uv.x, uv.y);
			color = faceColor;

			if(!meshes[ray.geomID]->material.hasLighting)
				return color;

			double refraction = meshes[ray.geomID]->material.refraction;
			double reflection = meshes[ray.geomID]->material.reflection;

			double p = (faceColor.x > faceColor.y && faceColor.x > faceColor.z) ? faceColor.x : (faceColor.y > faceColor.z ? faceColor.y : faceColor.z);
			if(reflection > 0.0 || refraction > 0.0)
				p = 1.0f;

			if(depth > MAX_RAY_DEPTH || !p) {
				if(GetRandomValue() < p)
					faceColor = faceColor * (1 / p);

				return faceColor;//meshes[ray.geomID]->getEmissionColor() * E;
			}

			Vec3fa n = meshes[ray.geomID]->getInterpolatedNormal(ray.primID, ray.u, ray.v);
			n = n.normalize();

			if(refraction > 0.0) {
				Vec3fa refractionResult = getRefraction(getHitPoint(ray), n, dir, depth);
				Vec3fa reflectionResult;
				
				if(reflection > 0.0)
					reflectionResult = getReflection(getHitPoint(ray), n, dir, ++depth);

				color = color * (1 - (reflection+refraction)) + (reflectionResult*reflection + refractionResult*refraction);
			} else if(meshes[ray.geomID]->material.emission == 0.0) {
				Vec3fa nl = n.dot(dir) < 0 ? n : n * -1.0f;
				Vec3fa nd = sampleAroundNormal(nl);

				Vec3fa recursiveRadiance = Vec3fa(0.0f);
				recursiveRadiance = getRadiance(point, nd, ++depth, 0);
				recursiveRadiance = faceColor * recursiveRadiance;

				Vec3fa lightsContrib = Vec3fa(0.0f);
				lightsContrib = faceColor * getLightContribution(ray, nl);
				color = meshes[ray.geomID]->getEmissionColor() * E + lightsContrib + recursiveRadiance;

				if(reflection > 0.0)
					color = color * (1.0f - reflection) + getReflection(getHitPoint(ray), n, dir, ++depth) * reflection;

				color = clip(color, 0.0, 1.0);
			}
		}
		return color;
	}

	Vec3fa getLightContribution(RTCRay preRay, Vec3fa normal) {
		Vec3fa point = getHitPoint(preRay);
		Vec3fa lightsContrib = Vec3fa(0.0);

		for (int i = 0; i < meshes.size(); i++) {
			if(meshes[i]->material.emission > 0.0) {
				Vec3fa lightPoint = meshes[i]->getRandomPointInMesh();
				Vec3fa rayDir = (lightPoint - point).normalize();
				RTCRay ray = getIntersection(sgScene, point, rayDir);

				double distanceEffect = 1.0 - ray.tfar / (meshes[i]->material.emission * 999.0);

				if(ray.geomID == meshes[i]->id)
					lightsContrib = lightsContrib + meshes[i]->getEmissionColor() * rayDir.dot(normal) * distanceEffect;
			}
		}

		return lightsContrib;
	}

	Vec3fa getReflection(Vec3fa point, Vec3fa normal, Vec3fa dir, int depth) {
		Vec3fa rayDir = dir - normal * 2.0f * normal.dot(dir);
		Vec3fa reflectionColor = Vec3fa(0.0f);

		reflectionColor = getRadiance(point, rayDir, depth);
	    return reflectionColor;
	}

	Vec3fa getRefraction(Vec3fa point, Vec3fa normal, Vec3fa dir, int depth) {
		Vec3fa nl = normal.dot(dir) < 0 ? normal : normal * -1.0;
		double into = normal.dot(nl);

		double refractiveIndexAir = 1;
		double refractiveIndexGlass = 1.5;
		double refractiveIndexRatio = into? refractiveIndexAir / refractiveIndexGlass : refractiveIndexGlass / refractiveIndexAir;
		double cosI = dir.dot(nl);
		double cos2t = 1 - refractiveIndexRatio * refractiveIndexRatio * (1 - cosI * cosI);

		if (cos2t < 0)
			return getRadiance(point, dir * -1.0f, ++depth);

		Vec3fa refractedDirection = dir * refractiveIndexRatio - normal * (into ? 1 : -1) * (cosI * refractiveIndexRatio + sqrt(cos2t));
		refractedDirection = refractedDirection.normalize();

		return getRadiance(point, refractedDirection, ++depth);
	}

	bool loadScene(const char* fileName, int width, int height) {
		imgWidth = width;
		imgHeight = height;

		ifstream data(fileName, ios::binary);

		Vec3fa cpos = readVec3fa(data);
		cpos.x = -cpos.x;
		Vec3fa cDir = (readVec3fa(data) - cpos).normalize();
		Vec3fa camRot = readVec3fa(data);

		double fov = readFloat(data);
		// fov = 360;
		cam = new Camera(cpos, camRot, cDir, fov, width, height);
		int nodeCount = readShort(data);

		for (int i = 0; i < nodeCount; i++) {
			SGRTMesh* m = new SGRTMesh(sgScene, data);
			meshes.push_back(m);
		}
		rtcCommit(sgScene);
		data.close();
	}

};

#endif
