/* Pre-launch map picker for server-hosting modes (Vanilla Server / Bot
   Server). Not used by the online client -- Teeworlds' own in-game vote
   menu (see teeworlds/votes.cfg) handles map switching there.

   Three columns, with a scrollbar past however many rows actually fit
   the screen (computed from the real window height at startup -- the
   R36S's 480px-tall display fits far fewer rows than the X55's 720px):
   Official (fixed 16 vanilla maps), Favorites (persisted, toggle with the
   F key), Others (everything else in data/maps/, alphabetical). Tab
   switches which column has input focus. L1/R1 (bound to PageUp/PageDown
   in map_picker.ini) jump the selection a full screen's worth of rows at
   once for fast scrolling through the Others column's ~1500 entries. A
   live search box (opened with the S key, or just start typing -- SDL
   text input is always active) filters the focused column. Enter confirms
   the highlighted map and writes it to the result file; Escape cancels
   (empty result, caller falls back to whatever sv_map is already in the
   .cfg). */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <SDL2/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_MAPS 4096
#define MAP_NAME_LEN 64
#define NUM_COLUMNS 3
#define DEFAULT_VISIBLE_ROWS 16
#define MIN_VISIBLE_ROWS 4
#define ROW_HEIGHT 34
#define COL_GAP 24
#define COL_SIDE_MARGIN 16
#define COL_MIN_WIDTH 120
#define COL_TOP 90
#define HINT_BAR_H 90
#define FONT_PIXEL_SIZE 20

/* How many rows actually fit the screen -- set once from the real window
   height right after SDL_CreateWindow (see main()), since a 480px-tall
   screen (R36S) fits far fewer than a 720px one (X55) and a fixed row
   count either overflows the window or leaves the hint bar off-screen. */
static int s_VisibleRows = DEFAULT_VISIBLE_ROWS;

enum { COL_OFFICIAL = 0, COL_FAVORITES = 1, COL_OTHERS = 2 };

typedef struct { char m_aName[MAP_NAME_LEN]; } CMapEntry;

typedef struct
{
	CMapEntry m_aEntries[MAX_MAPS];
	int m_NumEntries;
	int m_aFiltered[MAX_MAPS]; /* indices into m_aEntries matching the search */
	int m_NumFiltered;
	int m_ScrollOffset;
	int m_SelectedRow; /* row within the visible window */
	const char *m_pTitle;
} CColumn;

static const char *kOfficialMaps[] = {
	"dm1", "dm2", "dm3", "dm6", "dm7", "dm8", "dm9",
	"ctf1", "ctf2", "ctf3", "ctf4", "ctf5", "ctf6", "ctf7", "ctf8",
	"lms1"
};
#define NUM_OFFICIAL_MAPS (sizeof(kOfficialMaps) / sizeof(kOfficialMaps[0]))

static CColumn s_aColumns[NUM_COLUMNS];
static int s_FocusedColumn = 0;
static char s_aFavoritesPath[1024];
static char s_aSearchBuf[128] = "";
static int s_SearchLen = 0;

static SDL_Texture *LoadPngTexture(SDL_Renderer *pRenderer, const char *pPath, int *pW, int *pH)
{
	int Channels;
	unsigned char *pData = stbi_load(pPath, pW, pH, &Channels, 4);
	if(!pData)
	{
		fprintf(stderr, "map_picker: failed to load '%s': %s\n", pPath, stbi_failure_reason());
		return NULL;
	}
	SDL_Texture *pTexture = SDL_CreateTexture(pRenderer, SDL_PIXELFORMAT_ABGR8888,
		SDL_TEXTUREACCESS_STATIC, *pW, *pH);
	if(pTexture)
	{
		SDL_UpdateTexture(pTexture, NULL, pData, *pW * 4);
		SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_BLEND);
	}
	stbi_image_free(pData);
	return pTexture;
}

/* Scale-to-cover (fills the whole window, cropping overflow) rather than
   scale-to-fit -- this is a full-bleed background, letterbox bars would
   look wrong here. */
static void FitCover(int SrcW, int SrcH, int WinW, int WinH, SDL_Rect *pOut)
{
	float Scale = (float)WinW / SrcW;
	if(SrcH * Scale < WinH)
		Scale = (float)WinH / SrcH;
	int DrawW = (int)(SrcW * Scale);
	int DrawH = (int)(SrcH * Scale);
	pOut->x = (WinW - DrawW) / 2;
	pOut->y = (WinH - DrawH) / 2;
	pOut->w = DrawW;
	pOut->h = DrawH;
}

static int CompareMapEntries(const void *pA, const void *pB)
{
	return strcasecmp(((const CMapEntry *)pA)->m_aName, ((const CMapEntry *)pB)->m_aName);
}

static int IsOfficialMap(const char *pName)
{
	for(size_t i = 0; i < NUM_OFFICIAL_MAPS; i++)
		if(strcasecmp(pName, kOfficialMaps[i]) == 0)
			return 1;
	return 0;
}

static int FindFavoriteIndex(const char *pName)
{
	CColumn *pFav = &s_aColumns[COL_FAVORITES];
	for(int i = 0; i < pFav->m_NumEntries; i++)
		if(strcasecmp(pFav->m_aEntries[i].m_aName, pName) == 0)
			return i;
	return -1;
}

static void SaveFavorites(void)
{
	FILE *pFile = fopen(s_aFavoritesPath, "w");
	if(!pFile)
		return;
	CColumn *pFav = &s_aColumns[COL_FAVORITES];
	for(int i = 0; i < pFav->m_NumEntries; i++)
		fprintf(pFile, "%s\n", pFav->m_aEntries[i].m_aName);
	fclose(pFile);
}

/* Rebuilds the "Others" bucket (everything not Official/Favorites) and
   re-sorts it, called whenever Favorites changes since membership shifts
   entries between the two buckets. pAllMaps/NumAllMaps is the full
   directory scan, untouched. */
static CMapEntry s_aAllMaps[MAX_MAPS];
static int s_NumAllMaps;

static void RebuildOthersColumn(void)
{
	CColumn *pOthers = &s_aColumns[COL_OTHERS];
	pOthers->m_NumEntries = 0;
	for(int i = 0; i < s_NumAllMaps; i++)
	{
		const char *pName = s_aAllMaps[i].m_aName;
		if(IsOfficialMap(pName) || FindFavoriteIndex(pName) >= 0)
			continue;
		if(pOthers->m_NumEntries < MAX_MAPS)
			strncpy(pOthers->m_aEntries[pOthers->m_NumEntries++].m_aName, pName, MAP_NAME_LEN - 1);
	}
	qsort(pOthers->m_aEntries, pOthers->m_NumEntries, sizeof(CMapEntry), CompareMapEntries);
}

static void ApplyFilter(CColumn *pCol)
{
	if(s_SearchLen == 0)
	{
		pCol->m_NumFiltered = pCol->m_NumEntries;
		for(int i = 0; i < pCol->m_NumEntries; i++)
			pCol->m_aFiltered[i] = i;
	}
	else
	{
		pCol->m_NumFiltered = 0;
		for(int i = 0; i < pCol->m_NumEntries; i++)
		{
			/* crude case-insensitive substring match, good enough for map names */
			const char *pHay = pCol->m_aEntries[i].m_aName;
			int HayLen = (int)strlen(pHay);
			int Found = 0;
			for(int Start = 0; Start + s_SearchLen <= HayLen && !Found; Start++)
				if(strncasecmp(pHay + Start, s_aSearchBuf, s_SearchLen) == 0)
					Found = 1;
			if(Found)
				pCol->m_aFiltered[pCol->m_NumFiltered++] = i;
		}
	}
	if(pCol->m_ScrollOffset > pCol->m_NumFiltered - 1)
		pCol->m_ScrollOffset = pCol->m_NumFiltered > 0 ? pCol->m_NumFiltered - 1 : 0;
	if(pCol->m_SelectedRow >= pCol->m_NumFiltered)
		pCol->m_SelectedRow = pCol->m_NumFiltered > 0 ? pCol->m_NumFiltered - 1 : 0;
}

static void ApplyFilterAll(void)
{
	for(int c = 0; c < NUM_COLUMNS; c++)
		ApplyFilter(&s_aColumns[c]);
}

static const char *SelectedMapName(CColumn *pCol)
{
	int AbsRow = pCol->m_ScrollOffset + pCol->m_SelectedRow;
	if(AbsRow < 0 || AbsRow >= pCol->m_NumFiltered)
		return NULL;
	return pCol->m_aEntries[pCol->m_aFiltered[AbsRow]].m_aName;
}

static void MoveSelection(CColumn *pCol, int Delta)
{
	int AbsRow = pCol->m_ScrollOffset + pCol->m_SelectedRow + Delta;
	if(AbsRow < 0)
		AbsRow = 0;
	if(AbsRow > pCol->m_NumFiltered - 1)
		AbsRow = pCol->m_NumFiltered > 0 ? pCol->m_NumFiltered - 1 : 0;

	if(AbsRow < pCol->m_ScrollOffset)
		pCol->m_ScrollOffset = AbsRow;
	else if(AbsRow >= pCol->m_ScrollOffset + s_VisibleRows)
		pCol->m_ScrollOffset = AbsRow - s_VisibleRows + 1;
	pCol->m_SelectedRow = AbsRow - pCol->m_ScrollOffset;
}

/* ---- FreeType-backed text rendering: rasterize a whole string into an
   SDL texture on demand, no glyph cache. Fine at this screen's scale
   (a few dozen short strings, redrawn once per frame on a static menu). */
static FT_Library s_FtLibrary;
static FT_Face s_FtFace;

static SDL_Texture *RenderText(SDL_Renderer *pRenderer, const char *pText, SDL_Color Color, int *pW, int *pH)
{
	int Len = (int)strlen(pText);
	if(Len == 0)
	{
		*pW = 0;
		*pH = 0;
		return NULL;
	}

	/* First pass: figure out the pen-advance bounding box. */
	int PenX = 0;
	int MinY = 0, MaxY = FONT_PIXEL_SIZE;
	for(int i = 0; i < Len; i++)
	{
		if(FT_Load_Char(s_FtFace, (unsigned char)pText[i], FT_LOAD_RENDER))
			continue;
		FT_GlyphSlot pSlot = s_FtFace->glyph;
		PenX += pSlot->advance.x >> 6;
		int Top = pSlot->bitmap_top;
		int Bottom = Top - (int)pSlot->bitmap.rows;
		if(Top > MaxY)
			MaxY = Top;
		if(Bottom < MinY)
			MinY = Bottom;
	}
	int SurfW = PenX > 0 ? PenX : 1;
	int SurfH = MaxY - MinY;
	if(SurfH <= 0)
		SurfH = FONT_PIXEL_SIZE;

	unsigned char *pPixels = calloc((size_t)SurfW * SurfH, 4);
	if(!pPixels)
	{
		*pW = 0;
		*pH = 0;
		return NULL;
	}

	int Baseline = MaxY;
	PenX = 0;
	for(int i = 0; i < Len; i++)
	{
		if(FT_Load_Char(s_FtFace, (unsigned char)pText[i], FT_LOAD_RENDER))
			continue;
		FT_GlyphSlot pSlot = s_FtFace->glyph;
		FT_Bitmap *pBmp = &pSlot->bitmap;
		int OriginX = PenX + pSlot->bitmap_left;
		int OriginY = Baseline - pSlot->bitmap_top;
		for(unsigned int y = 0; y < pBmp->rows; y++)
		{
			int DestY = OriginY + (int)y;
			if(DestY < 0 || DestY >= SurfH)
				continue;
			for(unsigned int x = 0; x < pBmp->width; x++)
			{
				int DestX = OriginX + (int)x;
				if(DestX < 0 || DestX >= SurfW)
					continue;
				unsigned char Alpha = pBmp->buffer[y * pBmp->pitch + x];
				if(!Alpha)
					continue;
				unsigned char *pPixel = &pPixels[(DestY * SurfW + DestX) * 4];
				pPixel[0] = Color.r;
				pPixel[1] = Color.g;
				pPixel[2] = Color.b;
				pPixel[3] = Alpha;
			}
		}
		PenX += pSlot->advance.x >> 6;
	}

	SDL_Surface *pSurface = SDL_CreateRGBSurfaceWithFormatFrom(pPixels, SurfW, SurfH, 32, SurfW * 4, SDL_PIXELFORMAT_RGBA32);
	SDL_Texture *pTexture = pSurface ? SDL_CreateTextureFromSurface(pRenderer, pSurface) : NULL;
	if(pTexture)
		SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_BLEND);
	if(pSurface)
		SDL_FreeSurface(pSurface);
	free(pPixels);

	*pW = SurfW;
	*pH = SurfH;
	return pTexture;
}

static void DrawText(SDL_Renderer *pRenderer, const char *pText, int x, int y, SDL_Color Color)
{
	int w, h;
	SDL_Texture *pTexture = RenderText(pRenderer, pText, Color, &w, &h);
	if(!pTexture)
		return;
	SDL_Rect Dest = {x, y, w, h};
	SDL_RenderCopy(pRenderer, pTexture, NULL, &Dest);
	SDL_DestroyTexture(pTexture);
}

static void ScanMapsDirectory(const char *pMapsDir)
{
	DIR *pDir = opendir(pMapsDir);
	if(!pDir)
		return;
	struct dirent *pEntry;
	while((pEntry = readdir(pDir)) != NULL && s_NumAllMaps < MAX_MAPS)
	{
		size_t Len = strlen(pEntry->d_name);
		if(Len <= 4 || strcasecmp(pEntry->d_name + Len - 4, ".map") != 0)
			continue;
		size_t NameLen = Len - 4;
		if(NameLen >= MAP_NAME_LEN)
			NameLen = MAP_NAME_LEN - 1;
		strncpy(s_aAllMaps[s_NumAllMaps].m_aName, pEntry->d_name, NameLen);
		s_aAllMaps[s_NumAllMaps].m_aName[NameLen] = '\0';
		s_NumAllMaps++;
	}
	closedir(pDir);
}

static void LoadFavorites(void)
{
	CColumn *pFav = &s_aColumns[COL_FAVORITES];
	pFav->m_NumEntries = 0;
	FILE *pFile = fopen(s_aFavoritesPath, "r");
	if(!pFile)
		return;
	char aLine[MAP_NAME_LEN];
	while(fgets(aLine, sizeof(aLine), pFile) && pFav->m_NumEntries < MAX_MAPS)
	{
		size_t Len = strlen(aLine);
		while(Len > 0 && (aLine[Len - 1] == '\n' || aLine[Len - 1] == '\r'))
			aLine[--Len] = '\0';
		if(Len == 0)
			continue;
		strncpy(pFav->m_aEntries[pFav->m_NumEntries++].m_aName, aLine, MAP_NAME_LEN - 1);
	}
	fclose(pFile);
	qsort(pFav->m_aEntries, pFav->m_NumEntries, sizeof(CMapEntry), CompareMapEntries);
}

static void ToggleFavorite(const char *pName)
{
	CColumn *pFav = &s_aColumns[COL_FAVORITES];
	int Idx = FindFavoriteIndex(pName);
	if(Idx >= 0)
	{
		memmove(&pFav->m_aEntries[Idx], &pFav->m_aEntries[Idx + 1], (size_t)(pFav->m_NumEntries - Idx - 1) * sizeof(CMapEntry));
		pFav->m_NumEntries--;
	}
	else if(pFav->m_NumEntries < MAX_MAPS)
	{
		strncpy(pFav->m_aEntries[pFav->m_NumEntries++].m_aName, pName, MAP_NAME_LEN - 1);
		qsort(pFav->m_aEntries, pFav->m_NumEntries, sizeof(CMapEntry), CompareMapEntries);
	}
	SaveFavorites();
	RebuildOthersColumn();
	ApplyFilterAll();
}

int main(int argc, char **argv)
{
	if(argc < 5)
	{
		fprintf(stderr, "usage: %s <maps_dir> <favorites_file> <font_path> <bg_image> [output_file]\n", argv[0]);
		return 1;
	}
	const char *pMapsDir = argv[1];
	strncpy(s_aFavoritesPath, argv[2], sizeof(s_aFavoritesPath) - 1);
	const char *pFontPath = argv[3];
	const char *pBgPath = argv[4];
	const char *pOutputPath = argc > 5 ? argv[5] : getenv("MAP_PICKER_OUTPUT");

	if(FT_Init_FreeType(&s_FtLibrary))
	{
		fprintf(stderr, "map_picker: FT_Init_FreeType failed\n");
		return 1;
	}
	if(FT_New_Face(s_FtLibrary, pFontPath, 0, &s_FtFace))
	{
		fprintf(stderr, "map_picker: FT_New_Face failed for '%s'\n", pFontPath);
		return 1;
	}
	FT_Set_Pixel_Sizes(s_FtFace, 0, FONT_PIXEL_SIZE);

	if(SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		fprintf(stderr, "map_picker: SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Window *pWindow = SDL_CreateWindow("Teeworlds Map Picker",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
		SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
	if(!pWindow)
	{
		fprintf(stderr, "map_picker: SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Renderer *pRenderer = SDL_CreateRenderer(pWindow, -1, SDL_RENDERER_SOFTWARE);
	if(!pRenderer)
	{
		fprintf(stderr, "map_picker: SDL_CreateRenderer failed: %s\n", SDL_GetError());
		return 1;
	}

	{
		int StartupWinW, StartupWinH;
		SDL_GetWindowSize(pWindow, &StartupWinW, &StartupWinH);
		s_VisibleRows = (StartupWinH - COL_TOP - HINT_BAR_H) / ROW_HEIGHT;
		if(s_VisibleRows < MIN_VISIBLE_ROWS)
			s_VisibleRows = MIN_VISIBLE_ROWS;
	}

	int BgW = 0, BgH = 0;
	SDL_Texture *pBgTexture = LoadPngTexture(pRenderer, pBgPath, &BgW, &BgH);

	s_aColumns[COL_OFFICIAL].m_pTitle = "OFFICIAL";
	s_aColumns[COL_FAVORITES].m_pTitle = "FAVORITES";
	s_aColumns[COL_OTHERS].m_pTitle = "OTHERS";

	for(size_t i = 0; i < NUM_OFFICIAL_MAPS; i++)
		strncpy(s_aColumns[COL_OFFICIAL].m_aEntries[i].m_aName, kOfficialMaps[i], MAP_NAME_LEN - 1);
	s_aColumns[COL_OFFICIAL].m_NumEntries = (int)NUM_OFFICIAL_MAPS;

	ScanMapsDirectory(pMapsDir);
	LoadFavorites();
	RebuildOthersColumn();
	ApplyFilterAll();

	SDL_StartTextInput();

	int Running = 1;
	const char *pResult = NULL;
	int Cancelled = 0;

	while(Running)
	{
		SDL_Event Ev;
		while(SDL_PollEvent(&Ev))
		{
			if(Ev.type == SDL_QUIT)
			{
				Running = 0;
				Cancelled = 1;
			}
			else if(Ev.type == SDL_TEXTINPUT)
			{
				size_t AddLen = strlen(Ev.text.text);
				if(s_SearchLen + AddLen < sizeof(s_aSearchBuf) - 1)
				{
					memcpy(s_aSearchBuf + s_SearchLen, Ev.text.text, AddLen);
					s_SearchLen += (int)AddLen;
					s_aSearchBuf[s_SearchLen] = '\0';
					ApplyFilterAll();
				}
			}
			else if(Ev.type == SDL_KEYDOWN)
			{
				switch(Ev.key.keysym.sym)
				{
				case SDLK_UP:
					MoveSelection(&s_aColumns[s_FocusedColumn], -1);
					break;
				case SDLK_DOWN:
					MoveSelection(&s_aColumns[s_FocusedColumn], 1);
					break;
				case SDLK_PAGEUP:
					MoveSelection(&s_aColumns[s_FocusedColumn], -s_VisibleRows);
					break;
				case SDLK_PAGEDOWN:
					MoveSelection(&s_aColumns[s_FocusedColumn], s_VisibleRows);
					break;
				case SDLK_TAB:
					s_FocusedColumn = (s_FocusedColumn + 1) % NUM_COLUMNS;
					break;
				case SDLK_f:
				{
					const char *pName = SelectedMapName(&s_aColumns[s_FocusedColumn]);
					if(pName)
						ToggleFavorite(pName);
					break;
				}
				case SDLK_BACKSPACE:
					if(s_SearchLen > 0)
					{
						s_aSearchBuf[--s_SearchLen] = '\0';
						ApplyFilterAll();
					}
					break;
				case SDLK_RETURN:
				case SDLK_SPACE:
					pResult = SelectedMapName(&s_aColumns[s_FocusedColumn]);
					Running = 0;
					break;
				case SDLK_ESCAPE:
					Running = 0;
					Cancelled = 1;
					break;
				}
			}
		}

		SDL_SetRenderDrawColor(pRenderer, 18, 20, 26, 255);
		SDL_RenderClear(pRenderer);

		int WinW, WinH;
		SDL_GetWindowSize(pWindow, &WinW, &WinH);

		if(pBgTexture)
		{
			SDL_Rect BgDest;
			FitCover(BgW, BgH, WinW, WinH, &BgDest);
			SDL_RenderCopy(pRenderer, pBgTexture, NULL, &BgDest);
		}

		/* Semi-transparent dark panel behind the whole list area, so text
		   stays legible regardless of exactly what's under it in the
		   background art at that point. */
		{
			SDL_SetRenderDrawBlendMode(pRenderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(pRenderer, 10, 12, 18, 170);
			SDL_Rect Panel = {0, COL_TOP - 50, WinW, s_VisibleRows * ROW_HEIGHT + 140};
			SDL_RenderFillRect(pRenderer, &Panel);
		}
		int ColWidth = (WinW - 2 * COL_SIDE_MARGIN - (NUM_COLUMNS - 1) * COL_GAP) / NUM_COLUMNS;
		if(ColWidth < COL_MIN_WIDTH)
			ColWidth = COL_MIN_WIDTH;
		int TotalW = NUM_COLUMNS * ColWidth + (NUM_COLUMNS - 1) * COL_GAP;
		int StartX = (WinW - TotalW) / 2;

		SDL_Color TitleColor = {255, 210, 100, 255};
		SDL_Color RowColor = {220, 220, 220, 255};
		SDL_Color SelColor = {30, 30, 30, 255};

		for(int c = 0; c < NUM_COLUMNS; c++)
		{
			CColumn *pCol = &s_aColumns[c];
			int ColX = StartX + c * (ColWidth + COL_GAP);

			if(c == s_FocusedColumn)
			{
				SDL_SetRenderDrawColor(pRenderer, 255, 210, 100, 255);
				SDL_Rect Border = {ColX - 4, COL_TOP - 34, ColWidth + 8, s_VisibleRows * ROW_HEIGHT + 44};
				SDL_RenderDrawRect(pRenderer, &Border);
			}

			char aTitle[32];
			snprintf(aTitle, sizeof(aTitle), "%s (%d)", pCol->m_pTitle, pCol->m_NumFiltered);
			DrawText(pRenderer, aTitle, ColX, COL_TOP - 30, TitleColor);

			for(int Row = 0; Row < s_VisibleRows; Row++)
			{
				int AbsRow = pCol->m_ScrollOffset + Row;
				if(AbsRow >= pCol->m_NumFiltered)
					break;
				int RowY = COL_TOP + Row * ROW_HEIGHT;
				int IsSelected = (c == s_FocusedColumn && Row == pCol->m_SelectedRow);
				if(IsSelected)
				{
					SDL_SetRenderDrawColor(pRenderer, 255, 210, 100, 255);
					SDL_Rect Hl = {ColX, RowY, ColWidth, ROW_HEIGHT - 4};
					SDL_RenderFillRect(pRenderer, &Hl);
				}
				const char *pName = pCol->m_aEntries[pCol->m_aFiltered[AbsRow]].m_aName;
				char aLabel[MAP_NAME_LEN + 2];
				if(c != COL_FAVORITES && FindFavoriteIndex(pName) >= 0)
					snprintf(aLabel, sizeof(aLabel), "* %s", pName);
				else
					snprintf(aLabel, sizeof(aLabel), "%s", pName);
				DrawText(pRenderer, aLabel, ColX + 8, RowY + 4, IsSelected ? SelColor : RowColor);
			}

			/* scrollbar */
			if(pCol->m_NumFiltered > s_VisibleRows)
			{
				int TrackH = s_VisibleRows * ROW_HEIGHT;
				int TrackX = ColX + ColWidth + 6;
				SDL_SetRenderDrawColor(pRenderer, 60, 60, 70, 255);
				SDL_Rect Track = {TrackX, COL_TOP, 6, TrackH};
				SDL_RenderFillRect(pRenderer, &Track);

				int ThumbH = TrackH * s_VisibleRows / pCol->m_NumFiltered;
				if(ThumbH < 12)
					ThumbH = 12;
				int ThumbY = COL_TOP + (TrackH - ThumbH) * pCol->m_ScrollOffset / (pCol->m_NumFiltered - s_VisibleRows > 0 ? pCol->m_NumFiltered - s_VisibleRows : 1);
				SDL_SetRenderDrawColor(pRenderer, 255, 210, 100, 255);
				SDL_Rect Thumb = {TrackX, ThumbY, 6, ThumbH};
				SDL_RenderFillRect(pRenderer, &Thumb);
			}
		}

		/* Two short lines rather than one long one -- at this font size a
		   single-line hint (especially with L1/R1 added) runs well past
		   640px and would clip on the R36S's narrower screen. */
		char aHint1[128], aHint2[128];
		snprintf(aHint1, sizeof(aHint1), "Search: %s_   Tab: column   F: favorite", s_aSearchBuf);
		snprintf(aHint2, sizeof(aHint2), "L1/R1: page   Enter: pick   Esc: cancel");
		int HintY = COL_TOP + s_VisibleRows * ROW_HEIGHT + 30;
		DrawText(pRenderer, aHint1, StartX, HintY, TitleColor);
		DrawText(pRenderer, aHint2, StartX, HintY + FONT_PIXEL_SIZE + 8, TitleColor);

		SDL_RenderPresent(pRenderer);
		SDL_Delay(16);
	}

	SDL_StopTextInput();
	if(pBgTexture)
		SDL_DestroyTexture(pBgTexture);
	SDL_DestroyRenderer(pRenderer);
	SDL_DestroyWindow(pWindow);
	SDL_Quit();
	FT_Done_Face(s_FtFace);
	FT_Done_FreeType(s_FtLibrary);

	if(pOutputPath)
	{
		FILE *pFile = fopen(pOutputPath, "w");
		if(pFile)
		{
			if(!Cancelled && pResult)
				fprintf(pFile, "%s\n", pResult);
			fclose(pFile);
		}
	}

	return (Cancelled || !pResult) ? 1 : 0;
}
