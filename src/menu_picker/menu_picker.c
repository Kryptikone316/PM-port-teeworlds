/* Teeworlds launcher menu: background art + gamepad-navigable gold selector frame. */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_OPTIONS 4
#define SRC_W 1536
#define SRC_H 1024

typedef struct { int x, y, w, h; } Rect;

/* Button rects measured directly off the loader-menu.png source image (1536x1024). */
static const Rect kButtons[NUM_OPTIONS] = {
	{460, 312, 630, 150}, /* Vanilla (Client) */
	{460, 476, 630, 116}, /* Vanilla (Server) */
	{460, 610, 630, 120}, /* Bot Server */
	{460, 748, 630, 130}, /* Exit */
};

static SDL_Texture *load_png_texture(SDL_Renderer *pRenderer, const char *pPath)
{
	int width, height, channels;
	unsigned char *pData = stbi_load(pPath, &width, &height, &channels, 4);
	if(!pData)
	{
		fprintf(stderr, "menu_picker: failed to load '%s': %s\n", pPath, stbi_failure_reason());
		return NULL;
	}

	SDL_Texture *pTexture = SDL_CreateTexture(pRenderer, SDL_PIXELFORMAT_ABGR8888,
		SDL_TEXTUREACCESS_STATIC, width, height);
	if(pTexture)
	{
		SDL_UpdateTexture(pTexture, NULL, pData, width * 4);
		SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_BLEND);
	}
	stbi_image_free(pData);
	return pTexture;
}

static void fit_rect(int winW, int winH, SDL_Rect *pOut, float *pScale, int *pOffX, int *pOffY)
{
	float scale = winW / (float)SRC_W;
	float scaledH = SRC_H * scale;
	if(scaledH > winH)
	{
		scale = winH / (float)SRC_H;
	}
	int drawW = (int)(SRC_W * scale);
	int drawH = (int)(SRC_H * scale);
	pOut->x = (winW - drawW) / 2;
	pOut->y = (winH - drawH) / 2;
	pOut->w = drawW;
	pOut->h = drawH;
	*pScale = scale;
	*pOffX = pOut->x;
	*pOffY = pOut->y;
}

static void map_rect(const Rect *pSrc, float scale, int offX, int offY, int margin, SDL_Rect *pOut)
{
	pOut->x = offX + (int)((pSrc->x - margin) * scale);
	pOut->y = offY + (int)((pSrc->y - margin) * scale);
	pOut->w = (int)((pSrc->w + margin * 2) * scale);
	pOut->h = (int)((pSrc->h + margin * 2) * scale);
}

/* The source PNG has a lot of transparent padding around the actual frame art
   (887px canvas, only ~438px of it is opaque content) -- measured directly off
   the file. Scaling against the full canvas made the rendered frame look far
   thinner than intended, since half the destination rect was wasted on blank
   space. Crop to the real content box before doing any scaling. */
static const Rect kSelectorContent = {24, 212, 1728, 438};

/* Draw the selector frame as a 3-slice: fixed-aspect end caps, stretched middle bar.
   Avoids squashing the frame's rounded/decorated ends when it's stretched onto a
   button rect with a very different aspect ratio than the source image. */
static void draw_selector_3slice(SDL_Renderer *pRenderer, SDL_Texture *pSelector, const SDL_Rect *pDest)
{
	const Rect *pC = &kSelectorContent;
	const int capSrcW = 380; /* measured off the source PNG (content-relative): bracket+rivet ends here */
	int capDestW = (int)(capSrcW * (pDest->h / (float)pC->h));
	if(capDestW * 2 > pDest->w)
		capDestW = pDest->w / 2;

	SDL_Rect srcLeft = {pC->x, pC->y, capSrcW, pC->h};
	SDL_Rect srcMid = {pC->x + capSrcW, pC->y, pC->w - capSrcW * 2, pC->h};
	SDL_Rect srcRight = {pC->x + pC->w - capSrcW, pC->y, capSrcW, pC->h};

	SDL_Rect dstLeft = {pDest->x, pDest->y, capDestW, pDest->h};
	SDL_Rect dstMid = {pDest->x + capDestW, pDest->y, pDest->w - capDestW * 2, pDest->h};
	SDL_Rect dstRight = {pDest->x + pDest->w - capDestW, pDest->y, capDestW, pDest->h};

	SDL_RenderCopy(pRenderer, pSelector, &srcLeft, &dstLeft);
	SDL_RenderCopy(pRenderer, pSelector, &srcMid, &dstMid);
	SDL_RenderCopy(pRenderer, pSelector, &srcRight, &dstRight);
}

int main(int argc, char **argv)
{
	if(argc < 3)
	{
		fprintf(stderr, "usage: %s <background.png> <selector.png> [output_file]\n", argv[0]);
		return 1;
	}
	const char *pBackgroundPath = argv[1];
	const char *pSelectorPath = argv[2];
	const char *pOutputPath = argc > 3 ? argv[3] : getenv("PICKER_OUTPUT");

	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
	{
		fprintf(stderr, "menu_picker: SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GameController *pController = NULL;
	for(int i = 0; i < SDL_NumJoysticks(); i++)
	{
		if(SDL_IsGameController(i))
		{
			pController = SDL_GameControllerOpen(i);
			if(pController)
				break;
		}
	}

	SDL_Window *pWindow = SDL_CreateWindow("Teeworlds Launcher",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
		SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
	if(!pWindow)
	{
		fprintf(stderr, "menu_picker: SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Renderer *pRenderer = SDL_CreateRenderer(pWindow, -1, SDL_RENDERER_SOFTWARE);
	if(!pRenderer)
	{
		fprintf(stderr, "menu_picker: SDL_CreateRenderer failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Texture *pBackground = load_png_texture(pRenderer, pBackgroundPath);
	SDL_Texture *pSelector = load_png_texture(pRenderer, pSelectorPath);
	if(!pBackground || !pSelector)
	{
		return 1;
	}


	int selected = 0;
	int running = 1;
	int result = -1; /* -1 = cancelled */

	while(running)
	{
		SDL_Event ev;
		while(SDL_PollEvent(&ev))
		{
			if(ev.type == SDL_QUIT)
			{
				running = 0;
			}
			else if(ev.type == SDL_KEYDOWN)
			{
				switch(ev.key.keysym.sym)
				{
				case SDLK_UP: selected = (selected + NUM_OPTIONS - 1) % NUM_OPTIONS; break;
				case SDLK_DOWN: selected = (selected + 1) % NUM_OPTIONS; break;
				case SDLK_RETURN:
				case SDLK_SPACE: result = selected; running = 0; break;
				case SDLK_ESCAPE: result = -1; running = 0; break;
				}
			}
			else if(ev.type == SDL_CONTROLLERBUTTONDOWN)
			{
				switch(ev.cbutton.button)
				{
				case SDL_CONTROLLER_BUTTON_DPAD_UP: selected = (selected + NUM_OPTIONS - 1) % NUM_OPTIONS; break;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN: selected = (selected + 1) % NUM_OPTIONS; break;
				case SDL_CONTROLLER_BUTTON_A: result = selected; running = 0; break;
				case SDL_CONTROLLER_BUTTON_B:
				case SDL_CONTROLLER_BUTTON_BACK: result = -1; running = 0; break;
				}
			}
		}

		int winW, winH;
		SDL_GetWindowSize(pWindow, &winW, &winH);

		SDL_SetRenderDrawColor(pRenderer, 0, 0, 0, 255);
		SDL_RenderClear(pRenderer);

		SDL_Rect bgRect;
		float scale;
		int offX, offY;
		fit_rect(winW, winH, &bgRect, &scale, &offX, &offY);
		SDL_RenderCopy(pRenderer, pBackground, NULL, &bgRect);

		SDL_Rect selRect;
		map_rect(&kButtons[selected], scale, offX, offY, 8, &selRect);
		draw_selector_3slice(pRenderer, pSelector, &selRect);

		SDL_RenderPresent(pRenderer);
		SDL_Delay(16);
	}

	if(pController)
		SDL_GameControllerClose(pController);
	SDL_DestroyTexture(pBackground);
	SDL_DestroyTexture(pSelector);
	SDL_DestroyRenderer(pRenderer);
	SDL_DestroyWindow(pWindow);
	SDL_Quit();

	if(pOutputPath)
	{
		FILE *pFile = fopen(pOutputPath, "w");
		if(pFile)
		{
			fprintf(pFile, "%d\n", result);
			fclose(pFile);
		}
	}

	return result < 0 ? 1 : 0;
}
