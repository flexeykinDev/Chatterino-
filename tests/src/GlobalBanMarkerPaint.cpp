// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

/// Paints the cross-channel ban marker and looks at the pixels.
///
/// The arithmetic behind the marker is covered by GlobalBans.cpp, but
/// arithmetic passing is not the same as the tag being drawn — in the right
/// place, at the right size, with its number visible. A rect built inside out,
/// a brush left unset, or text drawn outside its own tag all satisfy every
/// assertion about numbers and produce nothing on screen.

#include "messages/layouts/MessageLayoutContainer.hpp"

#include "messages/layouts/MessageLayoutContext.hpp"
#include "messages/layouts/MessageLayoutElement.hpp"
#include "messages/MessageElement.hpp"
#include "messages/Selection.hpp"
#include "mocks/BaseApplication.hpp"
#include "providers/colors/ColorProvider.hpp"
#include "providers/companion/CompanionController.hpp"
#include "providers/companion/GlobalBanMarker.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Theme.hpp"
#include "Test.hpp"

#include "common/Literals.hpp"

#include <QImage>
#include <QPainter>

using namespace chatterino;
using namespace literals;

namespace {

class MockApplication : mock::BaseApplication
{
public:
    MockApplication()
        : theme(this->paths_)
        , fonts(this->settings)
    {
    }

    Theme *getThemes() override
    {
        return &this->theme;
    }

    Fonts *getFonts() override
    {
        return &this->fonts;
    }

    Theme theme;
    Fonts fonts;
};

constexpr int canvasWidth = 200;
constexpr int canvasHeight = 60;

/// The colour the canvas starts as, chosen so that neither the tag nor its
/// text can be mistaken for it.
const QColor background(0, 128, 0);

/// Points the mock at a service and answers one question about one chatter, so
/// the marker has a count to draw.
void seedMarker(int count)
{
    auto *companion = getApp()->getCompanion();
    companion->setBaseUrl("https://example.invalid/api");

    auto &bans = companion->globalBans();
    bans.note("11", "99");

    auto batch = bans.takeBatch();
    ASSERT_TRUE(batch.has_value());

    if (count > 0)
    {
        bans.applyMarkers("11", batch->userIds, {{"99", count}});
    }
    else
    {
        bans.applyMarkers("11", batch->userIds, {});
    }
}

/// Lays the marker out on its own and returns the container holding it.
void layOutMarker(MessageLayoutContainer &container,
                  GlobalBanMarkerElement &element)
{
    MessageLayoutContext ctx{
        .messageColors = {},
        .flags = MessageElementFlag::BadgeGlobalBan,
        .width = canvasWidth,
        .scale = 1.0F,
        .imageScale = 1.0F,
    };

    container.beginLayout(ctx.width, ctx.scale, ctx.imageScale, {});
    element.addToContainer(container, ctx);
    container.endLayout();
}

QImage paintContainer(const MessageLayoutContainer &container)
{
    QImage image(canvasWidth, canvasHeight, QImage::Format_ARGB32);
    image.fill(background);

    QPainter painter(&image);
    Selection selection;
    MessageColors colors;
    MessagePreferences preferences;

    MessagePaintContext ctx{
        .painter = painter,
        .selection = selection,
        .colorProvider = ColorProvider::instance(),
        .messageColors = colors,
        .preferences = preferences,
        .canvasWidth = canvasWidth,
        .isWindowFocused = true,
        .isMentions = false,
    };

    container.paintElements(painter, ctx);
    painter.end();

    return image;
}

/// The smallest rectangle holding every pixel that is not the background.
QRect paintedBounds(const QImage &image)
{
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;

    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixelColor(x, y) == background)
            {
                continue;
            }

            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    }

    if (right < 0)
    {
        return {};
    }

    return QRect(QPoint(left, top), QPoint(right, bottom));
}

int countMatching(const QImage &image, const QColor &colour, int tolerance)
{
    int found = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            auto pixel = image.pixelColor(x, y);
            if (std::abs(pixel.red() - colour.red()) <= tolerance &&
                std::abs(pixel.green() - colour.green()) <= tolerance &&
                std::abs(pixel.blue() - colour.blue()) <= tolerance)
            {
                found++;
            }
        }
    }

    return found;
}

}  // namespace

TEST(GlobalBanMarkerPaint, drawsATagWhereTheLayoutPutIt)
{
    MockApplication app;
    seedMarker(3);

    GlobalBanMarkerElement element("99", "11",
                                   MessageElementFlag::BadgeGlobalBan);
    MessageLayoutContainer container;
    layOutMarker(container, element);

    auto image = paintContainer(container);
    auto painted = paintedBounds(image);

    ASSERT_FALSE(painted.isEmpty())
        << "the marker laid out but drew nothing at all";

    // The ink has to land inside the space the line reserved for it. A tag
    // drawn outside its own slot looks fine on its own and overlaps whatever
    // sits next to it, which no arithmetic test would notice.
    auto *laid = container.getElementAt(QPointF(painted.center()));
    ASSERT_NE(laid, nullptr) << "nothing was laid out where the tag was drawn";

    auto slot = laid->getRect();
    EXPECT_GE(painted.left(), slot.left() - 1);
    EXPECT_LE(painted.right(), slot.right() + 1);
    EXPECT_GE(painted.top(), slot.top() - 1);
    EXPECT_LE(painted.bottom(), slot.bottom() + 1);

    // And it has to fill that slot rather than rattle around inside it.
    EXPECT_NEAR(painted.height(), slot.height(), 2);
    EXPECT_NEAR(painted.width(), slot.width(), 2);

    // The slot itself is the size the metrics asked for.
    EXPECT_NEAR(slot.height(), GlobalBanMarkerMetrics::size(18, 0).height(), 1);
}

TEST(GlobalBanMarkerPaint, fillsTheTagAndDrawsItsNumberInside)
{
    MockApplication app;
    seedMarker(3);

    GlobalBanMarkerElement element("99", "11",
                                   MessageElementFlag::BadgeGlobalBan);
    MessageLayoutContainer container;
    layOutMarker(container, element);

    auto image = paintContainer(container);

    // The tag's fill, and the darker digits on top of it. Both have to be
    // there: a tag with no number says nothing, and a number with no tag is
    // invisible against a light theme.
    auto fill = countMatching(image, QColor("#d9932b"), 12);
    auto text = countMatching(image, QColor("#1a1207"), 40);

    EXPECT_GT(fill, 100) << "the tag's fill is missing or nearly so";
    EXPECT_GT(text, 4) << "the count was not drawn on the tag";
    EXPECT_LT(text, fill) << "the digits cover more of the tag than its fill";
}

TEST(GlobalBanMarkerPaint, roundsItsCorners)
{
    MockApplication app;
    seedMarker(3);

    GlobalBanMarkerElement element("99", "11",
                                   MessageElementFlag::BadgeGlobalBan);
    MessageLayoutContainer container;
    layOutMarker(container, element);

    auto image = paintContainer(container);
    auto painted = paintedBounds(image);
    ASSERT_FALSE(painted.isEmpty());

    // The very corner pixel of the bounding box belongs to the background on a
    // rounded tag and to the fill on a square one, so this is what tells the
    // two apart.
    EXPECT_EQ(image.pixelColor(painted.left(), painted.top()), background);
    EXPECT_EQ(image.pixelColor(painted.right(), painted.top()), background);
}

TEST(GlobalBanMarkerPaint, growsWithTheNumberItCarries)
{
    MockApplication app;

    seedMarker(3);
    GlobalBanMarkerElement single("99", "11",
                                  MessageElementFlag::BadgeGlobalBan);
    MessageLayoutContainer singleContainer;
    layOutMarker(singleContainer, single);
    auto narrow = paintedBounds(paintContainer(singleContainer)).width();

    getApp()->getCompanion()->globalBans().clear();
    seedMarker(4321);
    GlobalBanMarkerElement many("99", "11",
                                MessageElementFlag::BadgeGlobalBan);
    MessageLayoutContainer manyContainer;
    layOutMarker(manyContainer, many);
    auto wide = paintedBounds(paintContainer(manyContainer)).width();

    EXPECT_GT(wide, narrow);
}

TEST(GlobalBanMarkerPaint, drawsNothingForAChatterWithNoBans)
{
    MockApplication app;
    seedMarker(0);

    GlobalBanMarkerElement element("99", "11",
                                   MessageElementFlag::BadgeGlobalBan);
    MessageLayoutContainer container;
    layOutMarker(container, element);

    // Not merely invisible: the element must take no space either, or every
    // message would carry a gap where a marker might one day go.
    EXPECT_EQ(container.getElementAt(QPointF(10, 8)), nullptr);
    EXPECT_TRUE(paintedBounds(paintContainer(container)).isEmpty());
}

TEST(GlobalBanMarkerPaint, drawsNothingWhileTheAnswerIsUnknown)
{
    MockApplication app;
    getApp()->getCompanion()->setBaseUrl("https://example.invalid/api");

    GlobalBanMarkerElement element("99", "11",
                                   MessageElementFlag::BadgeGlobalBan);
    MessageLayoutContainer container;
    layOutMarker(container, element);

    EXPECT_EQ(container.getElementAt(QPointF(10, 8)), nullptr);
    EXPECT_TRUE(paintedBounds(paintContainer(container)).isEmpty());
}

TEST(GlobalBanMarkerPaint, drawsNothingWhenTheServiceIsNotConfigured)
{
    MockApplication app;

    // Nothing is configured, so nothing can be known, so nothing is drawn —
    // this is what an ordinary install looks like.
    GlobalBanMarkerElement element("99", "11",
                                   MessageElementFlag::BadgeGlobalBan);
    MessageLayoutContainer container;
    layOutMarker(container, element);

    EXPECT_EQ(container.getElementAt(QPointF(10, 8)), nullptr);
    EXPECT_TRUE(paintedBounds(paintContainer(container)).isEmpty());
}

/// Writes a sheet of markers beside a name to the file named by
/// MARKER_PREVIEW, for looking at.
///
/// Disabled, because it produces no verdict — it exists because there is no
/// way to see this feature in a real chat without a Twitch account, and a tag
/// that is the right size in the wrong place is the kind of thing a person
/// spots in a second and an assertion never does. Run it with
/// `--gtest_also_run_disabled_tests`.
///
/// The text comes out as empty boxes: the headless test environment has no
/// font loaded that can draw it. The shapes and their placement are real.
TEST(GlobalBanMarkerPaint, DISABLED_dumpPreview)
{
    auto destination = qEnvironmentVariable("MARKER_PREVIEW");
    if (destination.isEmpty())
    {
        GTEST_SKIP() << "set MARKER_PREVIEW to the file to write";
    }

    MockApplication app;

    QImage sheet(420, 150, QImage::Format_ARGB32);
    sheet.fill(QColor("#18181b"));

    QPainter sheetPainter(&sheet);
    int y = 12;

    for (int count : {1, 3, 12, 4321})
    {
        getApp()->getCompanion()->globalBans().clear();
        seedMarker(count);

        TextElement name(u"someviewer"_s, MessageElementFlag::Username,
                         MessageColor(QColor("#7bb3ff")),
                         FontStyle::ChatMediumBold);
        GlobalBanMarkerElement marker("99", "11",
                                      MessageElementFlag::BadgeGlobalBan);
        TextElement body(u"is this thing on?"_s, MessageElementFlag::Text,
                         MessageColor(QColor("#e4e4e7")), FontStyle::ChatMedium);

        MessageLayoutContainer container;
        MessageLayoutContext ctx{
            .messageColors = {},
            .flags = MessageElementFlags{MessageElementFlag::BadgeGlobalBan,
                                         MessageElementFlag::Username,
                                         MessageElementFlag::Text},
            .width = canvasWidth * 2,
            .scale = 1.0F,
            .imageScale = 1.0F,
        };
        container.beginLayout(ctx.width, ctx.scale, ctx.imageScale, {});
        marker.addToContainer(container, ctx);
        name.addToContainer(container, ctx);
        body.addToContainer(container, ctx);
        container.endLayout();

        QImage row(canvasWidth * 2, 32, QImage::Format_ARGB32);
        row.fill(QColor("#18181b"));
        QPainter rowPainter(&row);
        Selection selection;
        MessageColors colors;
        MessagePreferences preferences;
        MessagePaintContext pctx{
            .painter = rowPainter,
            .selection = selection,
            .colorProvider = ColorProvider::instance(),
            .messageColors = colors,
            .preferences = preferences,
            .canvasWidth = canvasWidth * 2,
            .isWindowFocused = true,
            .isMentions = false,
        };
        container.paintElements(rowPainter, pctx);
        rowPainter.end();

        sheetPainter.drawImage(12, y, row);
        y += 34;
    }

    sheetPainter.end();
    ASSERT_TRUE(sheet.save(destination));
}
