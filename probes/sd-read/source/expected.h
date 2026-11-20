/* Generated from the supplied PC backup; metadata/hashes only. */
typedef struct { const char *path; unsigned long long offset, length, file_size; int whole; const char *sha256; } Check;
static const char expected_build_id[] = "652bfdc0375c2f663e56789da64c7bde00000000000000000000000000000000";
static const Check checks[] = {
    {"exefs/main", 0ULL, 75058793ULL, 75058793ULL, 1, "ddaf0958eacebb54996c43dae90346ca96e1df032111a1f1f56291b21bba3732"},
    {"exefs/main.npdm", 0ULL, 1828ULL, 1828ULL, 1, "97b05f29101575444d0ec91ebbf7f78935370645f1d672eb28a224b6723b298e"},
    {"exefs/rtld", 0ULL, 10477ULL, 10477ULL, 1, "b01d53da100a51eb40d408d7011cd5b09d7f4ee2578ef536d089855b51ee5e0f"},
    {"exefs/sdk", 0ULL, 7325246ULL, 7325246ULL, 1, "4617761033cab45951a19fa7797fa79ad691c46f324048f832484877d3df034c"},
    {"exefs/subsdk0", 0ULL, 40921266ULL, 40921266ULL, 1, "86fa5ec126d41f9c89bf332d84032476b033168f133cb8cdcc83ad12e33cbca4"},
    {"exefs/subsdk1", 0ULL, 6353337ULL, 6353337ULL, 1, "0c02d2050f52a26462e3859b15395749d0867fe418624538631dd8e7026dd2d8"},
    {"exefs/subsdk2", 0ULL, 749360ULL, 749360ULL, 1, "8f72c818479fa3410020a9c7bffe8dd2f6224d16f366bd0fe73adeb0201e0dee"},
    {"romfs/PJ045/Content/Paks/PJ045-Sage.ucas", 0ULL, 65536ULL, 3811317968ULL, 0, "065ce1b40d2d60fa7eb1d48cdf7c1fd505e50dd69991a8ebfd570f0eef5d097d"},
    {"romfs/PJ045/Content/Paks/PJ045-Sage.ucas", 1905658984ULL, 65536ULL, 3811317968ULL, 0, "446f3bd9790904f848a2275f6d916725d8738e4e8dea1acc092d4f322add9057"},
    {"romfs/PJ045/Content/Paks/PJ045-Sage.ucas", 3811252432ULL, 65536ULL, 3811317968ULL, 0, "65a919b3922b7bbd3cf4fa661a892f6da5f3c2d9e6f9eb976d5e241c3a0e7424"},
    {"romfs/PJ045/Content/Movies/OpeningDemo/source/Op_1080.mp4", 0ULL, 65536ULL, 304177832ULL, 0, "9373ac96b3195f2ebae4479f0f4b4ea36d7664731a7fd0421e28a0344befaa97"},
    {"romfs/PJ045/Content/Movies/OpeningDemo/source/Op_1080.mp4", 152088916ULL, 65536ULL, 304177832ULL, 0, "1829b40362357ca176d54349788f6bd8498daf589e7b0712884a6579df55e14c"},
    {"romfs/PJ045/Content/Movies/OpeningDemo/source/Op_1080.mp4", 304112296ULL, 65536ULL, 304177832ULL, 0, "a69b3bf79ea6e9137e54fdbc0b7279bd2c9d833e3065f820e2f8999ec0f1f640"},
};
#define CHECK_COUNT (sizeof(checks)/sizeof(checks[0]))
