#include "UI/Components/Dropdown.h"
#include <gtest/gtest.h>

using namespace Zenvra::UI;
using namespace Zenvra::UI::Components;

TEST(DropdownTests, InitialStateAndToggle) {
  Dropdown dropdown;
  EXPECT_FALSE(dropdown.is_open());

  const Rect anchor{100.0F, 100.0F, 200.0F, 30.0F};
  const Rect container{0.0F, 0.0F, 800.0F, 600.0F};

  dropdown.toggle(anchor, container, 1.0F);
  EXPECT_TRUE(dropdown.is_open());

  dropdown.toggle(anchor, container, 1.0F);
  EXPECT_FALSE(dropdown.is_open());
}

TEST(DropdownTests, LayoutPlacementBelowAndAbove) {
  Dropdown dropdown;
  std::vector<DropdownItem> items{
      {"dark", "Dark", "", {}, ""},
      {"light", "Light", "", {}, ""},
      {"contrast", "High Contrast", "", {}, ""},
  };
  dropdown.set_items(items);

  const Rect container{0.0F, 0.0F, 800.0F, 600.0F};

  // 1. Ample space below -> dropdown opens below anchor
  const Rect anchor_top{100.0F, 100.0F, 250.0F, 30.0F};
  dropdown.open(anchor_top, container, 1.0F);
  EXPECT_TRUE(dropdown.is_open());
  EXPECT_GE(dropdown.get_bounds().y, anchor_top.bottom());
  EXPECT_EQ(dropdown.get_layout_items().size(), 3);

  // 2. Cramped below, ample space above -> dropdown flips above anchor
  const Rect anchor_bottom{100.0F, 570.0F, 250.0F, 30.0F};
  dropdown.open(anchor_bottom, container, 1.0F);
  EXPECT_TRUE(dropdown.is_open());
  EXPECT_LE(dropdown.get_bounds().bottom(), anchor_bottom.y);
}

TEST(DropdownTests, ItemSelectionAndCallback) {
  Dropdown dropdown;
  std::vector<DropdownItem> items{
      {"dark", "Dark", "", {}, ""},
      {"one_dark_pro", "One Dark Pro", "", {}, ""},
      {"tokyo_night", "Tokyo Night", "", {}, ""},
  };
  dropdown.set_items(items);

  const Rect container{0.0F, 0.0F, 800.0F, 600.0F};
  const Rect anchor{100.0F, 100.0F, 200.0F, 30.0F};
  dropdown.open(anchor, container, 1.0F);

  std::string selected_id;
  dropdown.set_on_select([&selected_id](const DropdownItem &item) {
    selected_id = item.id;
  });

  const auto &layout_items = dropdown.get_layout_items();
  ASSERT_GE(layout_items.size(), 2);

  // Click on second item ("one_dark_pro")
  const auto &second_item = layout_items[1];
  const float click_x = second_item.bounds.x + 10.0F;
  const float click_y = second_item.bounds.y + 10.0F;

  EXPECT_TRUE(dropdown.handle_pointer_press(click_x, click_y, 1.0F));
  EXPECT_EQ(selected_id, "one_dark_pro");
  EXPECT_EQ(dropdown.get_selected_id(), "one_dark_pro");
  EXPECT_FALSE(dropdown.is_open());
}

TEST(DropdownTests, HoverAndScrolling) {
  Dropdown dropdown;
  std::vector<DropdownItem> items;
  for (int i = 0; i < 20; ++i) {
    items.push_back({"item_" + std::to_string(i), "Option " + std::to_string(i), "", {}, ""});
  }
  dropdown.set_items(items);

  const Rect container{0.0F, 0.0F, 800.0F, 600.0F};
  const Rect anchor{100.0F, 100.0F, 200.0F, 30.0F};
  dropdown.open(anchor, container, 1.0F);

  EXPECT_GT(dropdown.get_max_scroll(), 0.0F);

  // Hover first item
  const auto &first_item = dropdown.get_layout_items()[0];
  EXPECT_TRUE(dropdown.handle_pointer_move(first_item.bounds.x + 5.0F, first_item.bounds.y + 5.0F, 1.0F));

  // Scroll down with mouse wheel inside bounds
  const float start_scroll = dropdown.get_scroll_offset();
  EXPECT_TRUE(dropdown.handle_scroll(-1.0F, first_item.bounds.x + 5.0F, first_item.bounds.y + 5.0F, 1.0F));
  EXPECT_GT(dropdown.get_scroll_offset(), start_scroll);

  // Keyboard navigation
#if defined(_WIN32)
  dropdown.handle_key(VK_DOWN, 1.0F);
  EXPECT_GT(dropdown.get_scroll_offset(), start_scroll);

  // Escape closes dropdown
  dropdown.handle_key(VK_ESCAPE, 1.0F);
  EXPECT_FALSE(dropdown.is_open());
#endif
}

TEST(DropdownTests, ThemeSwatchesAndFontMetadata) {
  Dropdown dropdown;
  Theme::Color bg{40, 44, 52, 255};
  Theme::Color panel{33, 37, 43, 255};
  Theme::Color accent{97, 175, 239, 255};

  std::vector<DropdownItem> items{
      {"theme_one", "One Dark Pro", "", {bg, panel, accent}, ""},
      {"font_cascadia", "Cascadia Code", "Modern Windows Terminal", {}, "Cascadia Code"},
  };
  dropdown.set_items(items);

  const Rect container{0.0F, 0.0F, 800.0F, 600.0F};
  const Rect anchor{100.0F, 100.0F, 250.0F, 30.0F};
  dropdown.open(anchor, container, 1.0F);

  const auto &layout_items = dropdown.get_layout_items();
  ASSERT_EQ(layout_items.size(), 2);

  // Verify theme swatches preserved
  EXPECT_EQ(layout_items[0].item.preview_colors.size(), 3);
  EXPECT_EQ(layout_items[0].item.preview_colors[0], bg);

  // Verify font preview face and description preserved
  EXPECT_EQ(layout_items[1].item.preview_font_family, "Cascadia Code");
  EXPECT_EQ(layout_items[1].item.description, "Modern Windows Terminal");
}
