/*
** deko3d Examples - Main Menu
*/

// Sample Framework headers
#include "SampleFramework/CApplication.h"

// C++ standard library headers
#include <array>

void Test01();
void Test02();
void Test03();
void Test04();
void Test05();
void Test06();
void Test07();
void Test08();
void Test09();
void Test10();
void Test11();
void Test12();
void Test13();
void Test14();
void Test15();
void Test16();
void Test17();
void Test18();
void Test19();
void Test20();
void Test21();
void Test22();
void Test23();
void Test24();
void Test25();

namespace
{
    using ExampleFunc = void(*)(void);
    struct Example
    {
        ExampleFunc mainfunc;
        const char* name;
    };

    constexpr std::array Examples =
    {
        Example{ Test01, "01: 2D Array sequence"                       },
        Example{ Test02, "02: Linear texture raw writes"               },
        Example{ Test03, "03: Block linear texture raw writes"         },
        Example{ Test04, "04: Clear 2D mipmaps"                        },
        Example{ Test05, "05: Clear 3D slices"                         },
        Example{ Test06, "06: Clear 2D mipmaps lods"                   },
        Example{ Test07, "07: Clear 2D layers"                         },
        Example{ Test08, "08: Clear 2D layers no entries"              },
        Example{ Test09, "09: Clear 2D layers with mipmaps"            },
        Example{ Test10, "10: Compressed texture array"                },
        Example{ Test11, "11: sRGB upload"                             },
        Example{ Test12, "12: 3D upload"                               },
        Example{ Test13, "13: Base 2D"                                 },
        Example{ Test14, "14: Base 2D array"                           },
        Example{ Test15, "15: Base cube"                               },
        Example{ Test16, "16: Base cube array"                         },
        Example{ Test17, "17: Blit 3D images"                          },
        Example{ Test18, "18: Draw 3D slices"                          },
        Example{ Test19, "19: Draw 3D slices concurrent"               },
        Example{ Test20, "20: Read texture buffer"                     },
        Example{ Test21, "21: Clear size 0"                            },
        Example{ Test22, "22: Reversed array"                          },
        Example{ Test23, "23: Evil aliasing"                           },
        Example{ Test24, "24: Shadow compare R32"                      },
        Example{ Test25, "25: Color sample D32"                        },
    };
}

class CMainMenu final : public CApplication
{
    static constexpr unsigned EntriesPerScreen = 39;
    static constexpr unsigned EntryPageLength = 10;

    int screenPos;
    int selectPos;

    void renderMenu()
    {
        printf("\x1b[2J\n");
        printf("  deko3d Examples\n");
        printf("  Press PLUS(+) to exit; A to select an example to run\n");
        printf("\n");
        printf("--------------------------------------------------------------------------------");
        printf("\n");

        for (unsigned i = 0; i < (Examples.size() - screenPos) && i < EntriesPerScreen; i ++)
        {
            unsigned id = screenPos+i;
            printf("  %c %s\n", id==unsigned(selectPos) ? '*' : ' ', Examples[id].name);
        }
    }

    CMainMenu() : screenPos{}, selectPos{}
    {
        consoleInit(NULL);
        renderMenu();
    }

    ~CMainMenu()
    {
        consoleExit(NULL);
    }

    bool onFrame(u64 ns) override
    {
        int oldPos = selectPos;
        hidScanInput();

        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
        if (kDown & KEY_PLUS)
        {
            selectPos = -1;
            return false;
        }
        if (kDown & KEY_A)
            return false;
        if (kDown & KEY_UP)
            selectPos -= 1;
        if (kDown & KEY_DOWN)
            selectPos += 1;
        if (kDown & KEY_LEFT)
            selectPos -= EntryPageLength;
        if (kDown & KEY_RIGHT)
            selectPos += EntryPageLength;

        if (selectPos < 0)
            selectPos = 0;
        if (unsigned(selectPos) >= Examples.size())
            selectPos = Examples.size()-1;

        if (selectPos != oldPos)
        {
            if (selectPos < screenPos)
                screenPos = selectPos;
            else if (selectPos >= screenPos + int(EntriesPerScreen))
                screenPos = selectPos - EntriesPerScreen + 1;
            renderMenu();
        }

        consoleUpdate(NULL);
        return true;
    }

public:
    static ExampleFunc Display()
    {
        CMainMenu app;
        app.run();
        return app.selectPos >= 0 ? Examples[app.selectPos].mainfunc : nullptr;
    }
};

int main(int argc, char* argv[])
{
    for (;;)
    {
        ExampleFunc func = CMainMenu::Display();
        if (!func) break;
        func();
    }
    return 0;
}
