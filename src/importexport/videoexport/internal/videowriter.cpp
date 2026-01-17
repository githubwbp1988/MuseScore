/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "videowriter.h"

#include "videoencoder.h"

#include "engraving/dom/page.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/masterscore.h"

#include "notationscene/qml/MuseScore/NotationScene/playbackcursor.h"

#include "log.h"

#include <QPainter>

#include <chrono>
#include <thread>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace mu::iex::videoexport;
using namespace mu::project;
using namespace mu::notation;
using namespace muse::draw;
using namespace muse::midi;

// std::vector<IProjectWriter::UnitType> VideoWriter::supportedUnitTypes() const
std::vector<UnitType> VideoWriter::supportedUnitTypes() const
{
    return { UnitType::PER_PART };
}

bool VideoWriter::supportsUnitType(UnitType unitType) const
{
    std::vector<UnitType> unitTypes = supportedUnitTypes();
    return std::find(unitTypes.cbegin(), unitTypes.cend(), unitType) != unitTypes.cend();
}

muse::Ret VideoWriter::write(INotationProjectPtr, QIODevice&, const Options&)
{
    NOT_SUPPORTED;
    return make_ret(muse::Ret::Code::NotSupported);
}

muse::Ret VideoWriter::write(INotationProjectPtr project, const muse::io::path_t& filePath, const Options&)
{
    Config cfg;

    cfg.fps = configuration()->fps();

    std::string resolution = configuration()->resolution();
    if (resolution == "2160p") {
        cfg.width = 3840;
        cfg.height = 2160;
    } else if (resolution == "1440p") {
        cfg.width = 2560;
        cfg.height = 1440;
    } else if (resolution == "1080p") {
        cfg.width = 1920;
        cfg.height = 1080;
    } else if (resolution == "720p") {
        cfg.width = 1280;
        cfg.height = 720;
    } else if (resolution == "480p") {
        cfg.width = 854;
        cfg.height = 480;
    } else if (resolution == "360p") {
        cfg.width = 640;
        cfg.height = 360;
    } else {
        cfg.width = 1920;
        cfg.height = 1080;
    }

    // compute bitrate according to Google recommended settings
    // https://support.google.com/youtube/answer/1722171?hl=en
    float br = 8;
    if (cfg.height == 2160) {
        br = cfg.fps < 35 ? 40 : 60;
    } else if (cfg.height == 1440) {
        br = cfg.fps < 35 ? 16 : 24;
    } else if (cfg.height == 1080) {
        br = cfg.fps < 35 ? 10 : 15;
    } else if (cfg.height == 720) {
        br = cfg.fps < 35 ? 5 : 7.5;
    } else if (cfg.height == 480) {
        br = cfg.fps < 35 ? 2.5 : 4;
    } else if (cfg.height == 360) {
        br = cfg.fps < 35 ? 1 : 1.5;
    }
    cfg.bitrate = int(br * 1000000);

    cfg.leadingSec = configuration()->leadingSec();
    cfg.trailingSec = configuration()->trailingSec();

    muse::Ret ret = generatePagedOriginalVideo(project, filePath, cfg);
    return ret;
}

muse::Ret VideoWriter::generatePagedOriginalVideo(INotationProjectPtr project, const muse::io::path_t& filePath, const Config& config)
{
    // --score-video -o ./simple5.mp4 ./simple5.mscz

    VideoEncoder encoder;
    // if (!encoder.open(filePath, config.width, config.height, config.bitrate, config.fps / 2, config.fps)) {
    //! NOTE: The parameter gop here must be set to 0 with special attention, meaning each frame is a key frame.
    if (!encoder.open(filePath, config.width, config.height, config.bitrate, 0, config.fps)) {  // gop: config.fps / 2 -> 0
        LOGE() << "failed open encoder";
        return make_ret(muse::Ret::Code::UnknownError);
    }

    IMasterNotationPtr masterNotation = project->masterNotation();

    engraving::MasterScore* score = masterNotation->notation()->elements()->msScore()->masterScore();

    std::vector<QString> linesVec;
    bool isCoverLogoAvatar = true;
    bool isCoverTextAvatar = true;
    int coverAvatarLayout = 1;  // 0: left,  1: right   2: full  3: bottom(avatar below the text)
    int voffset = 0;
    int hoffset = 0;
    float voffsetScale = 0.0;
    float hoffsetScale = 0.0;
    float coverHoffsetScale = 0.0;
    int fontSize = 20;
    bool avatarFullHeight = false;
    int textHeightScale = 5;
    // QString avatarString = "/Users/erlich/Downloads/Tchaikovskys_Piano_Concerto_No._1_Opus_23_Arrangement_for_Solo_Piano.png";
    // QString avatarString = "/Users/erlich/Downloads/advanced_processed_1752139510167.png";
    // QString avatarString = "/Users/erlich/Downloads/simple_processed_xingxingdiandeng_cropped.png";
    QString avatarString = "/Users/erlich/Developer/workspace/python/pythonProject/test/test_images/Luffy_Fierce_Attack.png"; 

    // read Fine-tune parameters of video cover from a txt file
    // QString fineTunePath = "/Users/erlich/Downloads/musescore-fune-tune.txt";
    QString fineTunePath = audioConfiguration()->exportFineTuneConfigPath();
    QFile configFile(QString::fromStdWString(fineTunePath.toStdWString()));
    if (configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        isCoverLogoAvatar = false;
        isCoverTextAvatar = false;
        QByteArray data = configFile.readAll();
        QString content = QString::fromUtf8(data);
        QStringList lines = content.split("\n");
        for (const QString& line : lines) {
            QStringList keyValue = line.split("=");
            if (keyValue.size() == 2) {
                QString key = keyValue[0].trimmed();
                QString value = keyValue[1].trimmed();
                if (key == "title") {
                    linesVec.push_back(value);
                } else if (key == "isCoverLogoAvatar") {
                    isCoverLogoAvatar = (value.toLower() == "true");
                } else if (key == "isCoverTextAvatar") {
                    isCoverTextAvatar = (value.toLower() == "true");
                } else if (key == "coverAvatarLayout") {
                    coverAvatarLayout = value.toInt();
                } else if (key == "voffsetScale") {
                    voffsetScale = value.toFloat();
                } else if (key == "hoffsetScale") {
                    hoffsetScale = value.toFloat();
                } else if (key == "coverHoffsetScale") {
                    coverHoffsetScale = value.toFloat();
                } else if (key == "fontSize") {
                    fontSize = value.toInt();
                } else if (key == "avatarString") {
                    avatarString = value;
                } else if (key == "avatarFullHeight") {
                    avatarFullHeight = (value.toLower() == "true");
                } else if (key == "viewMode") {
                    if (value.toInt() == 0) {
                        masterNotation->notation()->setViewMode(notation::ViewMode::PAGE);
                    } else if (value.toInt() == 1) {
                        masterNotation->notation()->setViewMode(notation::ViewMode::FLOAT);
                    }
                } else if (key == "textHeightScale") {
                    textHeightScale = value.toInt();
                }
            }
        }
    } else {
        masterNotation->notation()->setViewMode(notation::ViewMode::FLOAT);
    }

    // Setup Score view
    score->setShowFrames(false);
    score->setShowInstrumentNames(true);
    score->setShowInvisible(false);
    score->setShowPageborders(false);
    score->setShowUnprintable(true);
    score->setShowVBox(false);

    score->doLayout();

    PageList pages = masterNotation->notation()->elements()->pages();
    if (pages.empty()) {
        LOGE() << "No pages";
        return make_ret(muse::Ret::Code::UnknownError);
    }

    double CANVAS_DPI = 300;

    int piano_height = 132 * config.width / config.height;  
    int keyboard_height = piano_height - 14 * config.width / config.height;

    const Page* page = pages.front();
    if (score->staves().size() > 3) {
        //! NOTE: Calculate the dpi to display all page elements
        muse::RectF ttbox = page->tbbox();
        double margin = 100.0;
        double ttboxHeight = ttbox.height() + margin * 2;
        double scale = (config.height - piano_height) / ttboxHeight;
        CANVAS_DPI = scale * engraving::DPI;
    }

    score->style().set(engraving::Sid::pageHeight, (config.height - piano_height) / CANVAS_DPI);
    double pageWidth = config.width / CANVAS_DPI;
    score->style().set(engraving::Sid::pageWidth, pageWidth);
    double pagePrintableWidth = score->style().styleD(engraving::Sid::pageWidth)
                        - score->style().styleD(engraving::Sid::pageOddLeftMargin)
                        - score->style().styleD(engraving::Sid::pageEvenLeftMargin);
    score->style().set(engraving::Sid::pagePrintableWidth, pagePrintableWidth);

    score->style().set(engraving::Sid::pageEvenTopMargin, 0.0);
    score->style().set(engraving::Sid::pageEvenBottomMargin, 0.0);
    score->style().set(engraving::Sid::pageOddTopMargin, 0.0);
    score->style().set(engraving::Sid::pageOddBottomMargin, 0.0);
    score->style().set(engraving::Sid::pageTwosided, false);
    score->style().set(engraving::Sid::showHeader, false);
    score->style().set(engraving::Sid::showFooter, false);

    score->style().set(engraving::Sid::minSystemDistance, engraving::Spatium(10));
    score->style().set(engraving::Sid::maxSystemDistance, engraving::Spatium(10));
    score->style().set(engraving::Sid::staffLowerBorder, engraving::Spatium(5));
    score->style().set(engraving::Sid::staffUpperBorder, engraving::Spatium(7));

    score->setLayoutAll();
    score->update();

    // Setup painting
    QImage frame(config.width, config.height, QImage::Format_RGB32);
    frame.setDotsPerMeterX(std::lrint((CANVAS_DPI * 1000) / engraving::INCH));
    frame.setDotsPerMeterY(std::lrint((CANVAS_DPI * 1000) / engraving::INCH));

    QPainter qp(&frame);
    qp.setRenderHint(QPainter::Antialiasing, true);
    qp.setRenderHint(QPainter::TextAntialiasing, true);
    muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));

    double pianoHeight = piano_height * frameRect.height() / config.height;
    muse::RectF pianoRect = muse::RectF(frameRect.x(), frameRect.bottom() - pianoHeight, 
                                        frameRect.width(), pianoHeight);

    double keyboardHeight = keyboard_height * frameRect.height() / config.height;
    muse::RectF keyboardRect = muse::RectF(frameRect.x(), frameRect.bottom() - keyboardHeight, 
                                        frameRect.width(), keyboardHeight);

    muse::RectF pianoTopBorderRect = muse::RectF(frameRect.x(), frameRect.bottom() - pianoHeight, 
                                        frameRect.width(), pianoHeight - keyboardHeight);

    const Color PIANO_BG_COLOR = Color(54, 54, 56, 255);
    const Color KEYBOARD_BG_COLOR = Color(36, 36, 39, 255);

    Painter painter(&qp, "video_writer");

    qreal keyboard_scale = m_trickFunction(&qp, frameRect.toQRectF(), keyboardRect.toQRectF());

    piano_height = piano_height * keyboard_scale;
    keyboard_height = keyboard_height * keyboard_scale;
    pianoHeight = pianoHeight * keyboard_scale + 4;
    keyboardHeight = keyboardHeight * keyboard_scale + 4;

    pianoRect.setY(frameRect.bottom() - pianoHeight);
    pianoRect.setHeight(pianoHeight);

    keyboardRect.setY(frameRect.bottom() - keyboardHeight);
    keyboardRect.setHeight(keyboardHeight);

    pianoTopBorderRect.setY(frameRect.bottom() - pianoHeight);
    pianoTopBorderRect.setHeight(pianoHeight - keyboardHeight);

    auto painting = masterNotation->notation()->painting();

    // Setup duration
    INotationPlaybackPtr playback = masterNotation->playback();
    float totalPlayTimeSec = playback->totalPlayTime();

    LOGI() << "totalPlayTime: " << totalPlayTimeSec << " sec";

    // int frameCount = (totalPlayTimeSec + config.leadingSec + config.trailingSec) * config.fps;
    int frameCount = (totalPlayTimeSec + config.leadingSec) * config.fps;

    //! NOTE: After setting the score above, the number of pages may change - get them again
    pages = masterNotation->notation()->elements()->pages();

    auto pageByTick = [](const PageList& pages, tick_t tick) -> const Page* {
        for (const Page* p : pages) {
            if (tick <= static_cast<tick_t>(p->endTick().ticks())) {
                return p;
            }
        }
        return nullptr;
    };

    const Color CURSOR_COLOR = Color(0, 0, 255, 100);

    PlaybackCursor cursor(application()->iocContext());
    cursor.setNotation(masterNotation->notation());

    // QString workTitle = score->metaTag(u"workTitle");
    // QString subtitle = score->metaTag(u"subtitle");
    // QString composer = score->metaTag(u"composer");
    // QString arranger = score->metaTag(u"arranger");

    // QString line1 = workTitle;
    // QString line2 = subtitle;
    // QString line3 = arranger;
    
    // if (!composer.isEmpty()) {
    //     if (line3.isEmpty()) {
    //         line3 = composer;
    //     } else {
    //         line3 = arranger + "  " + composer; 
    //     }
    // }

    // if (!line1.isEmpty()) {
    //     linesVec.push_back(line1);
    // }
    // if (!line2.isEmpty()) {
    //     linesVec.push_back(line2);
    // }
    // if (!line3.isEmpty()) {
    //     linesVec.push_back(line3);
    // }

    // linesVec.clear();
    // // linesVec.push_back("No. 1 in B♭ Minor, Opus 23");
    // linesVec.push_back("Entrance to the Infinity Castle");
    // // linesVec.push_back("My Fatherland - The Moldau");
    // linesVec.push_back("Demon Slayer");
    // linesVec.push_back("Transcribed by Erlich");

    int lines = linesVec.size();

    // QFont::Light Normal Medium DemiBold Bold
    QFont font("Comic Sans MS", fontSize, QFont::Normal);   // 38 - Medium  48 - 2160p  28 - Normal
    QFontMetrics fm(font);

    // high_resolution_clock::time_point startTp = high_resolution_clock::now();

    // Flag to check if the leading frame has been saved
    bool leadingFrameSaved = false;

    for (int f = 0; f < frameCount; f++) {
        float currentTimeSec = (qreal)f / config.fps;
        
        // while (true) {
        //     auto now = high_resolution_clock::now();
        //     double elapsedSec = duration_cast<duration<double>>(now - startTp).count();

        //     if (elapsedSec >= currentTimeSec) {
        //         break;
        //     }

        //     // Use sleep when the time interval is relatively long to reduce CPU usage.
        //     if (currentTimeSec - elapsedSec > 0.002) { // 2ms 
        //         std::this_thread::sleep_for(500us); // Microsecond-level sleep
        //     } else {
        //         // Use spin-waiting when approaching the target time to improve accuracy.
        //         std::this_thread::yield();
        //     }
        // }
        
        if (currentTimeSec < config.leadingSec) {
            int textHeight = fm.height();
            int totalTextHeight = lines * textHeight;
            qp.setFont(font);
            qp.setPen(Qt::white);
            QRectF rect = frameRect.toQRectF();

            if (isCoverLogoAvatar) {
                // logo avatar
                qreal centerX = rect.center().x();
                qreal centerY = rect.center().y();
                textHeight = textHeight * 5;

                QImage avatar(avatarString);
                int avatarSize = textHeight * 8;
                if (!avatar.isNull()) {
                    avatar = avatar.scaledToHeight(avatarSize, Qt::SmoothTransformation);

                    qreal xStart = centerX - avatar.width() / 2;
                    qreal yStart = centerY - avatar.height() / 2;
                    
                    qp.drawImage(QPointF(xStart, yStart), avatar);
                }

            } else {
                if (!isCoverTextAvatar) {
                    // text
                    int yStart = rect.center().y() - totalTextHeight;
                    int t_scale = 1;
                    if (linesVec.size() >= 6) {
                        t_scale = 2;
                    }
                    yStart -= textHeight * 6 * t_scale;
                    for (int i = 0; i < lines; i++) {
                        QString line = linesVec[i];
                        QRectF textRect = rect;
                        textRect.moveTop(yStart + i * textHeight * 5);
                        qp.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, line);
                    }
                } else {
                    // avatar + text
                    qreal centerX = rect.center().x();
                    qreal centerY = rect.center().y();
                    textHeight = textHeight * textHeightScale;
                    totalTextHeight = totalTextHeight * textHeightScale;

                    QImage avatar(avatarString);  // 
                    int avatarSize = textHeight * 8;  // 6
                    if (avatarFullHeight) {
                        avatarSize = rect.height();
                    }
                    if (!avatar.isNull()) {
                        if (coverAvatarLayout == 2) {
                            avatar = avatar.scaledToWidth(rect.width(), Qt::SmoothTransformation);
                            QRect cropRect(0, avatar.height() - rect.height(), rect.width(), rect.height());
                            avatar = avatar.copy(cropRect);
                        } else {
                            // avatar = avatar.scaled(avatarSize, avatarSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                            avatar = avatar.scaledToHeight(avatarSize, Qt::SmoothTransformation);
                        }
                    }

                    int maxTextWidth = 0;
                    for (const QString& line : linesVec) {
                        int lineWidth = fm.horizontalAdvance(line);
                        lineWidth = lineWidth * 5;
                        if (lineWidth > maxTextWidth) {
                            maxTextWidth = lineWidth;
                        }
                    }
                    
                    qreal spacing = 0; // 20
                    qreal totalWidth = avatar.width() + spacing + maxTextWidth;
                    if (coverAvatarLayout == 3) {
                        totalWidth = avatar.width() > maxTextWidth ? avatar.width() : maxTextWidth;
                    }
                    qreal xStart = centerX - totalWidth / 2;
                    if (coverAvatarLayout == 1) {
                        xStart = centerX + totalWidth / 2 - avatar.width();
                    } else if (coverAvatarLayout == 3) {
                        xStart = centerX - avatar.width() / 2;
                    }
                    qreal yStart = centerY - totalTextHeight / 2;
                    if (coverAvatarLayout == 3) {
                        qreal verti_total_height = totalTextHeight + spacing + avatar.height();
                        yStart = centerY + verti_total_height / 2 - avatar.height();
                    }
                    if (!avatar.isNull()) {
                        if (coverAvatarLayout == 2) {
                            qp.drawImage(rect.topLeft(), avatar);
                        } else if (coverAvatarLayout == 3) {
                            qp.drawImage(QPointF(xStart, yStart), avatar);
                        } else {
                            // if (coverAvatarLayout == 0) {
                            //     // xStart += 0.4 * totalTextHeight;
                            // } else if (coverAvatarLayout == 1) {
                            //     // xStart -= 0.4 * totalTextHeight;
                            // }
                            xStart += coverHoffsetScale * totalTextHeight;
                            if (xStart < 0) {
                                xStart = 0;
                            }
                            qp.drawImage(QPointF(xStart, yStart + (totalTextHeight - avatar.height()) / 2), avatar);
                        }
                    }

                    voffset = voffsetScale * totalTextHeight;
                    hoffset = hoffsetScale * totalTextHeight;

                    if (coverAvatarLayout == 2) {
                        // voffset = -0.5 * totalTextHeight;
                        // hoffset = 0.5 * totalTextHeight;
                        for (int i = 0; i < lines; ++i) {
                            QString line = linesVec[i];
                            qreal textX = centerX - maxTextWidth / 2 + hoffset; 
                            qreal textY = yStart + i * textHeight + voffset;

                            QRectF textRect(textX, textY, maxTextWidth, textHeight);
                            qp.drawText(textRect, Qt::AlignCenter | Qt::AlignVCenter, line);
                        }
                    } else if (coverAvatarLayout == 3) {
                        for (int i = 0; i < lines; ++i) {
                            QString line = linesVec[i];
                            qreal textX = centerX - maxTextWidth / 2; 
                            qreal textY = yStart - spacing - (lines - i) * textHeight;
                            QRectF textRect(textX, textY, maxTextWidth, textHeight);
                            qp.drawText(textRect, Qt::AlignCenter, line);
                        }
                    } else {
                        // if (coverAvatarLayout == 0) {
                        //     voffset = -0.2 * totalTextHeight;
                        //     hoffset = -0.2 * totalTextHeight;
                        // } else if (coverAvatarLayout == 1) { 
                        //     voffset = -0.2 * totalTextHeight;
                        //     hoffset = 0.5 * totalTextHeight;
                        // }

                        for (int i = 0; i < lines; ++i) {
                            QString line = linesVec[i];
                            qreal textX = xStart + avatar.width() + spacing + hoffset; 
                            if (coverAvatarLayout == 1) {
                                textX = xStart - spacing - maxTextWidth + hoffset;
                            }
                            qreal textY = yStart + i * textHeight + voffset;

                            QRectF textRect(textX, textY, maxTextWidth, textHeight);
                            qp.drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, line);
                        }
                    }
                }
            }

            // Save the first frame of the leading section as an image
            if (!leadingFrameSaved) {
                QString imageFile = filePath.toQString();
                imageFile.replace(".mp4", ".png");

                frame.save(imageFile);
                LOGI() << "Leading frame saved as image: " << imageFile.toStdString();
                leadingFrameSaved = true;
            }

            encoder.encodeImage(frame);

            continue;
        }
        currentTimeSec -= config.leadingSec;

        tick_t tick = playback->secToTick(currentTimeSec);

        const Page* page = pageByTick(pages, tick);
        if (!page) {
            break;
        }
        INotationPainting::Options opt;
        opt.fromPage = page->no();
        opt.toPage = opt.fromPage;
        opt.deviceDpi = CANVAS_DPI;

        painter.fillRect(frameRect, Color::WHITE);

        painter.save();
        painting->paintPrint(&painter, opt);
        cursor.move(tick);
        muse::RectF cursorRect = cursor.rect();
        muse::PointF pagePos = page->pos();
        muse::RectF cursorAbsRect = cursorRect.translated(-pagePos);
        painter.fillRect(cursorAbsRect, CURSOR_COLOR);
        painter.restore();

        // painter.fillRect(pianoRect, PIANO_BG_COLOR);
        // painter.fillRect(keyboardRect, KEYBOARD_BG_COLOR);
        painter.fillRect(pianoTopBorderRect, PIANO_BG_COLOR);

        painter.save();
        m_invokeFunction();
        painter.restore();

        encoder.encodeImage(frame);
    }

    encoder.close();
    m_trickOffFunction();
    return muse::make_ok();
}

void VideoWriter::pianoViewTrick(std::function<qreal(QPainter*, QRectF, QRectF)> trickFunction) {
    m_trickFunction = trickFunction;
}

void VideoWriter::pianoViewTrickOff(std::function<void()> trickOffFunction) {
    m_trickOffFunction = trickOffFunction;
}

void VideoWriter::pianoViewInvoke(std::function<void()> invokeFunction) {
    m_invokeFunction = invokeFunction;
}
