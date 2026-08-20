/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include <chrono>
#include <cmath>

#include <QPainter>
#include <QThread>

#include "global/concurrency/concurrent.h"
#include "global/io/filestream.h"

#include "draw/fontmetrics.h"
#include "draw/painter.h"
#include "draw/types/drawtypes.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/page.h"
#include "engraving/dom/repeatlist.h"

#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/inotationpainting.h"
#include "notation/inotationplayback.h"
#include "notation/notationtypes.h"
#include "notationscene/qml/MuseScore/NotationScene/playbackcursor.h"

#include "defer.h"
#include "log.h"

using namespace mu::iex::videoexport;
using namespace mu::project;
using namespace mu::notation;
using namespace muse::draw;
using namespace muse::midi;

static muse::String notationTitle(const INotationPtr notation)
{
    muse::String title;
    mu::engraving::Score* score = notation->elements()->msScore();
    mu::engraving::MasterScore* masterScore = notation->masterNotation()->masterScore();

    if (const mu::engraving::Text* text = score->getText(mu::engraving::TextStyleType::TITLE)) {
        title = text->plainText();
    }

    if (title.isEmpty()) {
        if (const mu::engraving::Text* text = masterScore->getText(mu::engraving::TextStyleType::TITLE)) {
            title = text->plainText();
        }
    }

    if (title.isEmpty()) {
        title = masterScore->metaTag(u"workTitle");
    }

    return title;
}

static muse::String notationSubtitle(const INotationPtr notation)
{
    if (notation->isMaster()) {
        return muse::String();
    }

    return notation->name();
}

std::vector<INotationWriter::UnitType> VideoWriter::supportedUnitTypes() const
{
    return { UnitType::PER_PART };
}

bool VideoWriter::supportsUnitType(UnitType unitType) const
{
    std::vector<UnitType> unitTypes = supportedUnitTypes();
    return std::find(unitTypes.cbegin(), unitTypes.cend(), unitType) != unitTypes.cend();
}

muse::Ret VideoWriter::write(INotationPtr notation, muse::io::IODevice& device, const INotationWriter::Options& options)
{
    std::string filePath = device.meta("file_path");
    IF_ASSERT_FAILED(!filePath.empty()) {
        return make_ret(muse::Ret::Code::InternalError);
    }

    muse::io::path_t _imageFile(filePath);
    QString imageFile = _imageFile.toQString();
    imageFile.replace(".mp4", ".png");
    cover_path = std::make_shared<QString>(imageFile);

    bool withAudio = muse::value(options, INotationWriter::OptionKey::WITH_AUDIO, muse::Val(true)).toBool();

    Config cfg = makeConfig();

    muse::io::path_t finalPath(filePath);
    muse::io::path_t tempAudioPath = finalPath + ".tmp_audio.aac";

    auto encoder = videoEncodeResolver()->currentVideoEncoder();

    muse::media::IVideoEncoder::Options encoderOptions;
    encoderOptions.format = "mp4";
    encoderOptions.width = cfg.width;
    encoderOptions.height = cfg.height;
    encoderOptions.bitrate = cfg.bitrate;
    // ! NOTE: The parameter gop here must be set to 0 with special attention, meaning each frame is a key frame.
    // gop: config.fps / 2 -> 0
    // encoderOptions.gop = cfg.fps / 2;
    encoderOptions.gop = 0;
    encoderOptions.fps = cfg.fps;

    if (!encoder->open(finalPath, encoderOptions)) {
        LOGE() << "failed to open video encoder";
        return make_ret(muse::Ret::Code::UnknownError);
    }

    m_isCompleted = false;
    m_audioCompleted = false;
    m_abort = false;
    m_writeRet = muse::Ret();
    m_audioRet = muse::Ret();

    startVideoExport(encoder, notation, cfg);

    if (withAudio) {
        startAudioExport(notation, tempAudioPath, cfg);
    } else {
        m_audioCompleted = true;
        m_audioRet = muse::make_ok();
    }

    while (!m_isCompleted || !m_audioCompleted) {
        application()->processEvents();
        QThread::yieldCurrentThread();
    }

    if (m_audioWriter) {
        m_audioWriter->progress()->finished().disconnect(this);
        m_audioWriter = nullptr;
    }

    if (m_audioFile) {
        m_audioFile->close();
        m_audioFile.reset();
    }

    muse::Ret result = m_writeRet;

    encoder->finishEncode();

    // Release the device's file handle before add audio replaces the file
    device.close();

    if (withAudio) {
        if (result && m_audioRet) {
            if (!encoder->addAudio(tempAudioPath)) {
                result = make_ret(muse::Ret::Code::UnknownError);
            }
        } else if (!result) {
            // keep video error
        } else {
            result = m_audioRet;
        }

        fileSystem()->remove(tempAudioPath);
    }

    encoder->close();

    return result;
}

VideoWriter::Config VideoWriter::makeConfig() const
{
    Config cfg;

    cfg.fps = configuration()->fps();

    std::string resolution = configuration()->resolution();
    if (resolution == "2160p") {
        cfg.width = 3840;
        cfg.height = 2160;
        cfg.resolution_level = 3;
    } else if (resolution == "1440p") {
        cfg.width = 2560;
        cfg.height = 1440;
        cfg.resolution_level = 2;
    } else if (resolution == "1080p") {
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.resolution_level = 1;
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
        cfg.resolution_level = 1;
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
    cfg.trailingSec = configuration()->trailingSec() > 0 ? configuration()->trailingSec() : cfg.trailingSec;

    cfg.viewMode = configuration()->viewMode();

    return cfg;
}

void VideoWriter::startVideoExport(muse::media::IVideoEncoderPtr encoder, INotationPtr notation, const Config& cfg)
{
    muse::Concurrent::run([this, encoder, notation, cfg]() {
        doGenerate(encoder, notation, cfg);
    });
}

void VideoWriter::startAudioExport(INotationPtr notation, const muse::io::path_t& audioPath, const Config& cfg)
{
    m_audioWriter = writers()->writer("aac");
    if (!m_audioWriter) {
        LOGE() << "aac writer not found";
        m_audioRet = make_ret(muse::Ret::Code::InternalError);
        m_audioCompleted = true;
        return;
    }

    m_audioWriter->progress()->finished().onReceive(this, [this](const muse::ProgressResult& res) {
        m_audioRet = res.ret;
        m_audioCompleted = true;
    });

    INotationWriter::Options audioOpts;
    audioOpts[INotationWriter::OptionKey::WAIT_FOR_COMPLETION] = muse::Val(false);
    audioOpts[INotationWriter::OptionKey::LEADING_SILENCE_SEC] = muse::Val(static_cast<double>(cfg.leadingSec));
    audioOpts[INotationWriter::OptionKey::TRAILING_SILENCE_SEC] = muse::Val(static_cast<double>(cfg.trailingSec));

    m_audioFile = std::make_unique<muse::io::FileStream>(audioPath);
    m_audioFile->setMeta("file_path", audioPath.toStdString());
    m_audioFile->open(muse::io::IODevice::WriteOnly);

    m_audioWriter->write(notation, *m_audioFile, audioOpts);
}

muse::Ret VideoWriter::writeList(const INotationPtrList&, muse::io::IODevice&, const INotationWriter::Options&)
{
    NOT_SUPPORTED;
    return make_ret(muse::Ret::Code::NotSupported);
}

muse::Progress* VideoWriter::progress()
{
    return &m_progress;
}

void VideoWriter::abort()
{
    m_abort = true;

    if (m_exportAbortFunction) {
        m_exportAbortFunction();
        m_exportAbortFunction = nullptr;
    }

    if (m_audioWriter) {
        m_audioWriter->abort();
    }

    // Wait abort completion
    while (!m_isCompleted || !m_audioCompleted) {
        application()->processEvents();
        QThread::yieldCurrentThread();
    }
}

std::optional<VideoWriter::ScoreRestoreData> VideoWriter::prepareScore(INotationPtr notation, Config& config)
{
    ScoreRestoreData result;
    engraving::Score* score = notation->elements()->msScore();

    result.style = score->style();

    result.layoutMode = score->layoutMode();

    result.showFrames = score->showFrames();
    result.showInstrumentNames = score->showInstrumentNames();
    result.showInvisible = score->isShowInvisible();
    result.showPageborders = score->showPageborders();
    result.showUnprintable = score->showUnprintable();
    result.showVBox = score->layoutOptions().isShowVBox;

    score->setLayoutMode(engraving::LayoutMode::PAGE);

    score->setShowFrames(false);
    score->setShowInstrumentNames(true);
    score->setShowInvisible(false);
    score->setShowPageborders(false);
    score->setShowUnprintable(false);

    if (config.viewMode == ViewMode::Flexible) {
        score->setShowInstrumentNames(false);
        score->setShowVBox(false);
    }

    score->doLayout();

    PageList pages = notation->elements()->pages();
    if (pages.empty()) {
        LOGE() << "No pages";
        restoreScore(notation, result);
        return std::nullopt;
    }

    const Page* page = pages.front();

    if (config.viewMode == ViewMode::PageFull) {
        double scaleX = config.width / page->width();
        double scaleY = config.height / page->height();
        double scale = std::min(scaleX, scaleY);
        config.canvasDpi = scale * engraving::DPI;
        config.moveToCenter = muse::PointF(0.5 * (config.width / scale - page->width()), 0.5 * (config.height / scale - page->height()));
        return result;
    }

    _piano_height = 132 * config.width / config.height;  
    _keyboard_height = _piano_height - 14 * config.width / config.height;

    if (score->staves().size() > 3) {
        //! NOTE: Calculate the dpi to display all page elements
        double originalPageHeight = page->height() - score->style().styleD(engraving::Sid::pageOddTopMargin)
                                    - score->style().styleD(engraving::Sid::pageOddBottomMargin);
        double margin = 100.0;
        double ttboxHeight = originalPageHeight + margin * 2;
        double scale = (config.height - _piano_height) / ttboxHeight;
        config.canvasDpi = scale * engraving::DPI;
    }

    score->style().set(engraving::Sid::pageHeight, (config.height - _piano_height) / config.canvasDpi);
    score->style().set(engraving::Sid::pageWidth, config.width / config.canvasDpi);
    score->style().set(engraving::Sid::pagePrintableWidth, score->style().styleD(engraving::Sid::pageWidth)
                       - score->style().styleD(engraving::Sid::pageOddLeftMargin)
                       - score->style().styleD(engraving::Sid::pageEvenLeftMargin));

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
    score->doLayout();

    return result;
}

void VideoWriter::restoreScore(INotationPtr notation, const ScoreRestoreData& data)
{
    engraving::Score* score = notation->elements()->msScore();

    score->style() = data.style;

    score->setShowFrames(data.showFrames);
    score->setShowInstrumentNames(data.showInstrumentNames);
    score->setShowInvisible(data.showInvisible);
    score->setShowPageborders(data.showPageborders);
    score->setShowUnprintable(data.showUnprintable);
    score->setShowVBox(data.showVBox);

    score->setLayoutMode(data.layoutMode);

    score->setLayoutAll();
    score->update();
}

void VideoWriter::doGenerate(muse::media::IVideoEncoderPtr encoder, INotationPtr notation, const Config& config)
{
    Config actualConfig = config;
    auto restoreData = prepareScore(notation, actualConfig);
    if (!restoreData) {
        m_writeRet = make_ret(muse::Ret::Code::UnknownError);
        m_isCompleted = true;
        return;
    }

    DEFER {
        restoreScore(notation, restoreData.value());
        m_isCompleted = true;
    };

    // Setup painting
    QImage frame(actualConfig.width, actualConfig.height, QImage::Format_RGB32);
    frame.setDotsPerMeterX(std::lrint((actualConfig.canvasDpi * 1000) / engraving::INCH));
    frame.setDotsPerMeterY(std::lrint((actualConfig.canvasDpi * 1000) / engraving::INCH));

    QPainter qp(&frame);
    qp.setRenderHint(QPainter::Antialiasing, true);
    qp.setRenderHint(QPainter::TextAntialiasing, true);

    Painter painter(&qp, "video_writer");

    muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));
    pianoRect = muse::RectF(frameRect.x(), frameRect.bottom() - _piano_height, 
                                        frameRect.width(), _piano_height);

    keyboardRect = muse::RectF(frameRect.x(), frameRect.bottom() - _keyboard_height - 1, 
                                        frameRect.width(), _keyboard_height);
    qreal keyboard_scale = m_trickFunction(&qp, frameRect.toQRectF(), keyboardRect.toQRectF());

    double pianoHeight = _piano_height * frameRect.height() / config.height * keyboard_scale;
    double keyboardHeight = _keyboard_height * keyboard_scale;

    pianoRect = muse::RectF(frameRect.x(), frameRect.bottom() - pianoHeight, 
                                        frameRect.width(), pianoHeight);

    keyboardRect = muse::RectF(frameRect.x(), frameRect.bottom() - keyboardHeight, 
                                        frameRect.width(), keyboardHeight);

    m_adjustTrickFunction(keyboardRect.toQRectF(), config.resolution_level);

    pianoTopBorderRect = muse::RectF(frameRect.x(), frameRect.bottom() - pianoHeight, 
                                        frameRect.width(), pianoHeight - keyboardHeight);

    // Setup duration
    INotationPlaybackPtr playback = notation->masterNotation()->playback();
    float totalPlayTimeSec = playback->totalPlayTime();

    int leadingFrameCount = static_cast<int>(actualConfig.leadingSec * actualConfig.fps);
    int scoreFrameCount = static_cast<int>(totalPlayTimeSec * actualConfig.fps);
    int trailingFrameCount = static_cast<int>(actualConfig.trailingSec * actualConfig.fps);
    int totalFrameCount = leadingFrameCount + scoreFrameCount + trailingFrameCount;
    LOGI() << "totalPlayTime: " << totalPlayTimeSec << " sec" << ", frame count: " << totalFrameCount;

    m_progress.start();

    // Add score title
    if (!generateLeadingFrames(encoder, notation, &qp, painter, frame, actualConfig, totalFrameCount)) {
        return;
    }

    // Add score frames
    if (!generateScoreFrames(encoder, notation, painter, frame, actualConfig, totalPlayTimeSec, leadingFrameCount, totalFrameCount)) {
        return;
    }

    // Add "Made with MuseScore"
    if (!generateTrailingFrames(encoder, actualConfig)) {
        return;
    }

    m_writeRet = muse::make_ok();
    m_progress.finish(muse::make_ok());
}

bool VideoWriter::generateLeadingFrames(muse::media::IVideoEncoderPtr encoder, INotationPtr notation,
                                        QPainter* qp, Painter& painter, QImage& frame,
                                        const Config& config, int totalFrameCount)
{
    // int leadingFrameCount = static_cast<int>(config.leadingSec * config.fps);
    // if (leadingFrameCount <= 0) {
    //     return true;
    // }

    // muse::String title = notationTitle(notation);
    // muse::String subtitle = notationSubtitle(notation);

    // auto scaledFontPointSize = [&config](double basePixelSize) {
    //     double pixelSize = basePixelSize * config.height / 1080.0;
    //     return pixelSize * 72.0 / engraving::DPI;
    // };

    // Font titleFont(Font::FontFamily(u"Muse Sans"), Font::Type::Text);
    // titleFont.setPointSizeF(scaledFontPointSize(128.0));
    // titleFont.setWeight(Font::Weight::Medium);

    // Font subtitleFont(titleFont);
    // subtitleFont.setPointSizeF(scaledFontPointSize(48.0));

    // muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));

    // const double maxTextWidth = frameRect.width() * 0.9;
    // const double textLeft = (frameRect.width() - maxTextWidth) / 2.0;
    
    // auto lineCount = [&](const FontMetrics& fm, const muse::String& text) {
    //     if (text.isEmpty()) {
    //         return 0;
    //     }
    //     double textWidth = fm.horizontalAdvance(text);
    //     return std::max(1, static_cast<int>(std::ceil(textWidth / maxTextWidth)));
    // };

    // FontMetrics titleFontMetrics(titleFont);
    // const int titleLines = lineCount(titleFontMetrics, title);
    // const double titleHeight = titleLines * titleFontMetrics.lineSpacing();

    // FontMetrics subtitleFontMetrics(subtitleFont);
    // const int subtitleLines = lineCount(subtitleFontMetrics, subtitle);
    // const double subtitleHeight = subtitleLines * subtitleFontMetrics.lineSpacing();

    // double centerY = frameRect.center().y();
    // double titleTop = centerY - titleHeight / 2.0;
    // muse::RectF titleRect(textLeft, titleTop, maxTextWidth, titleHeight);

    // const double subtitleOffset = config.height / 20.0;
    // double subtitleTop = titleRect.bottom() + subtitleOffset;
    // muse::RectF subtitleRect(textLeft, subtitleTop, maxTextWidth, subtitleHeight);

    // for (int f = 0; f < leadingFrameCount; f++) {
    //     if (m_abort) {
    //         m_writeRet = make_ret(muse::Ret::Code::Cancel);
    //         m_progress.finish(m_writeRet);
    //         return false;
    //     }

    //     m_progress.progress(f, totalFrameCount);

    //     painter.fillRect(frameRect, Color::BLACK);
    //     painter.setPen(Color::WHITE);
    //     painter.setFont(titleFont);
    //     painter.drawText(titleRect, AlignCenter, TextWordWrap, title);

    //     if (!subtitle.isEmpty()) {
    //         painter.setFont(subtitleFont);
    //         painter.drawText(subtitleRect, AlignCenter, TextWordWrap, subtitle);
    //     }

    //     encoder->encodeImage(frame);
    // }

    // return true;
    ////////////////////////////////////////////////
    // custom expand
    int leadingFrameCount = static_cast<int>(config.leadingSec * config.fps);
    if (leadingFrameCount <= 0) {
        return true;
    }

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
    QString avatarString = ""; 

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
                        notation->masterNotation()->notation()->setViewMode(notation::ViewMode::PAGE);
                    } else if (value.toInt() == 1) {
                        notation->masterNotation()->notation()->setViewMode(notation::ViewMode::FLOAT);
                    }
                } else if (key == "textHeightScale") {
                    textHeightScale = value.toInt();
                }
            }
        }
    } else {
        notation->masterNotation()->notation()->setViewMode(notation::ViewMode::FLOAT);
    }

    // QString workTitle = score->metaTag(u"workTitle");
    // QString subtitle = score->metaTag(u"subtitle");
    // QString composer = score->metaTag(u"composer");
    // QString arranger = score->metaTag(u"arranger");

    int lines = linesVec.size();

    // QFont::Light Normal Medium DemiBold Bold
    QFont font("Comic Sans MS", fontSize, QFont::Normal);   // 38 - Medium  48 - 2160p  28 - Normal
    QFontMetrics fm(font);

    // Flag to check if the leading frame has been saved
    bool leadingFrameSaved = false;
    muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));

    for (int f = 0; f < leadingFrameCount; f++) {
        if (m_abort) {
            m_writeRet = make_ret(muse::Ret::Code::Cancel);
            m_progress.finish(m_writeRet);
            return false;
        }
        int textHeight = fm.height();
        int totalTextHeight = lines * textHeight;
        qp->setFont(font);
        qp->setPen(Qt::white);
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
                
                qp->drawImage(QPointF(xStart, yStart), avatar);
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
                    qp->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, line);
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
                        qp->drawImage(rect.topLeft(), avatar);
                    } else if (coverAvatarLayout == 3) {
                        qp->drawImage(QPointF(xStart, yStart), avatar);
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
                        qp->drawImage(QPointF(xStart, yStart + (totalTextHeight - avatar.height()) / 2), avatar);
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
                        qp->drawText(textRect, Qt::AlignCenter | Qt::AlignVCenter, line);
                    }
                } else if (coverAvatarLayout == 3) {
                    for (int i = 0; i < lines; ++i) {
                        QString line = linesVec[i];
                        qreal textX = centerX - maxTextWidth / 2; 
                        qreal textY = yStart - spacing - (lines - i) * textHeight;
                        QRectF textRect(textX, textY, maxTextWidth, textHeight);
                        qp->drawText(textRect, Qt::AlignCenter, line);
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
                        qp->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter, line);
                    }
                }
            }
        }

        // Save the first frame of the leading section as an image
        if (!leadingFrameSaved) {
            frame.save(*cover_path);

            LOGI() << "Leading frame saved as image: " << cover_path->toStdString();
            leadingFrameSaved = true;
        }

        encoder->encodeImage(frame);
    }
    return true;
}

bool VideoWriter::generateTrailingFrames(muse::media::IVideoEncoderPtr encoder, const Config& config)
{
    int trailingFrameCount = static_cast<int>(config.trailingSec * config.fps);
    if (trailingFrameCount <= 0) {
        return true;
    }

    if (m_abort) {
        m_writeRet = make_ret(muse::Ret::Code::Cancel);
        m_progress.finish(m_writeRet);
        return false;
    }

    static const muse::io::path_t RESOURCE_PATH = ":/videoexport/internal/resources/video_made_with.mp4";
    muse::ByteArray videoData = fileSystem()->readFile(RESOURCE_PATH).val;

    encoder->encodeVideo(videoData, trailingFrameCount);

    return true;
}

bool VideoWriter::generateScoreFrames(muse::media::IVideoEncoderPtr encoder, INotationPtr notation,
                                      Painter& painter, QImage& frame,
                                      const Config& config, float totalPlayTimeSec,
                                      int leadingFrameCount, int totalFrameCount)
{
    int scoreFrameCount = static_cast<int>(totalPlayTimeSec * config.fps);
    if (scoreFrameCount <= 0) {
        return true;
    }

    PageList pages = notation->elements()->pages();
    auto painting = notation->painting();
    INotationPlaybackPtr playback = notation->masterNotation()->playback();
    muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));

    auto pageByTick = [](const PageList& pages, tick_t tick) -> const Page* {
        for (const Page* p : pages) {
            if (tick < static_cast<tick_t>(p->endTick().ticks())) {
                return p;
            }
        }
        return nullptr;
    };

    // const Color CURSOR_COLOR = Color(0, 0, 255, 100);
    const Color CURSOR_COLOR = Color(2, 109, 203, 127);

    PlaybackCursor cursor(iocContext());
    cursor.setNotation(notation);
    cursor.enableVideoExport(true);
    m_exportAbortFunction = [&cursor]() {
        cursor.enableVideoExport(false);
    };
    
    for (int f = 0; f < scoreFrameCount; f++) {
        if (m_abort) {
            m_writeRet = make_ret(muse::Ret::Code::Cancel);
            m_progress.finish(m_writeRet);
            return false;
        }

        m_progress.progress(leadingFrameCount + f, totalFrameCount);

        float currentTimeSec = static_cast<float>(f) / config.fps;
        if (currentTimeSec > totalPlayTimeSec) {
            currentTimeSec = totalPlayTimeSec;
        }

        tick_t tick = playback->secToTick(currentTimeSec);

        const Page* page = pageByTick(pages, tick);
        if (!page) {
            break;
        }

        if (!page->firstMeasure()) {
            // Skip pages with no notation
            continue;
        }

        INotationPainting::Options opt;
        opt.fromPage = static_cast<int>(page->pageNumber());
        opt.toPage = opt.fromPage;
        opt.deviceDpi = config.canvasDpi;

        painter.fillRect(frameRect, Color::BLACK);

        painter.save();
        painter.translate(config.moveToCenter);

        painting->paintPrint(&painter, opt);

        cursor.move(tick);
        std::this_thread::sleep_for(std::chrono::microseconds(50000));

        muse::RectF cursorRect = cursor.rect();
        muse::PointF pagePos = page->pos();
        muse::RectF cursorAbsRect = cursorRect.translated(-pagePos);
        painter.fillRect(cursorAbsRect, CURSOR_COLOR);
        painter.restore();

        painter.fillRect(pianoRect, PIANO_BG_COLOR);
        painter.fillRect(keyboardRect, KEYBOARD_BG_COLOR);
        painter.fillRect(pianoTopBorderRect, PIANO_BG_COLOR);

        painter.save();
        m_invokeFunction();
        std::this_thread::sleep_for(std::chrono::microseconds(50000));
        painter.restore();

        encoder->encodeImage(frame);
    }
    cursor.enableVideoExport(false);
    m_trickOffFunction();
    m_exportAbortFunction = nullptr;
    return true;
}

void VideoWriter::pianoViewTrick(std::function<qreal(QPainter*, QRectF, QRectF)> trickFunction) {
    m_trickFunction = trickFunction;
}

void VideoWriter::adjustPianoViewTrick(std::function<void(QRectF, int)> trickFunction) {
    m_adjustTrickFunction = trickFunction;
}

void VideoWriter::pianoViewTrickOff(std::function<void()> trickOffFunction) {
    m_trickOffFunction = trickOffFunction;
}

void VideoWriter::pianoViewInvoke(std::function<void()> invokeFunction) {
    m_invokeFunction = invokeFunction;
}
