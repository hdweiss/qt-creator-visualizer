/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#include "basetextdocumentlayout.h"
#include <utils/qtcassert.h>

using namespace TextEditor;

namespace TextEditor {
namespace Internal {

class DocumentMarker : public ITextMarkable
{
    Q_OBJECT
public:
    DocumentMarker(QTextDocument *);
    ~DocumentMarker();

    TextMarks marks() const { return m_marksCache; }

    // ITextMarkable
    bool addMark(ITextMark *mark);
    TextMarks marksAt(int line) const;
    void removeMark(ITextMark *mark);
    void updateMark(ITextMark *mark);

private:
    double recalculateMaxMarkWidthFactor() const;

    TextMarks m_marksCache; // not owned
    QTextDocument *document;
};

DocumentMarker::DocumentMarker(QTextDocument *doc)
  : ITextMarkable(doc), document(doc)
{
}

DocumentMarker::~DocumentMarker()
{

}

bool DocumentMarker::addMark(TextEditor::ITextMark *mark)
{
    QTC_ASSERT(mark->lineNumber() >= 1, return false);
    int blockNumber = mark->lineNumber() - 1;
    BaseTextDocumentLayout *documentLayout =
        qobject_cast<BaseTextDocumentLayout*>(document->documentLayout());
    QTC_ASSERT(documentLayout, return false);
    QTextBlock block = document->findBlockByNumber(blockNumber);

    if (block.isValid()) {
        TextBlockUserData *userData = BaseTextDocumentLayout::userData(block);
        userData->addMark(mark);
        m_marksCache.append(mark);
        mark->updateLineNumber(blockNumber + 1);
        QTC_CHECK(mark->lineNumber() == blockNumber + 1);
        mark->updateBlock(block);
        documentLayout->hasMarks = true;
        documentLayout->maxMarkWidthFactor = qMax(mark->widthFactor(),
            documentLayout->maxMarkWidthFactor);
        documentLayout->requestUpdate();
        return true;
    }
    return false;
}

double DocumentMarker::recalculateMaxMarkWidthFactor() const
{
    double maxWidthFactor = 1.0;
    foreach (const ITextMark *mark, marks())
        maxWidthFactor = qMax(mark->widthFactor(), maxWidthFactor);
    return maxWidthFactor;
}

TextEditor::TextMarks DocumentMarker::marksAt(int line) const
{
    QTC_ASSERT(line >= 1, return TextMarks());
    int blockNumber = line - 1;
    QTextBlock block = document->findBlockByNumber(blockNumber);

    if (block.isValid()) {
        if (TextBlockUserData *userData = BaseTextDocumentLayout::testUserData(block))
            return userData->marks();
    }
    return TextMarks();
}

void DocumentMarker::removeMark(TextEditor::ITextMark *mark)
{
    BaseTextDocumentLayout *documentLayout =
        qobject_cast<BaseTextDocumentLayout*>(document->documentLayout());
    QTC_ASSERT(documentLayout, return)

    bool needUpdate = false;
    QTextBlock block = document->begin();
    while (block.isValid()) {
        if (TextBlockUserData *data = static_cast<TextBlockUserData *>(block.userData())) {
            needUpdate |= data->removeMark(mark);
        }
        block = block.next();
    }
    m_marksCache.removeAll(mark);

    if (needUpdate) {
        documentLayout->maxMarkWidthFactor = recalculateMaxMarkWidthFactor();
        updateMark(0);
    }
}

void DocumentMarker::updateMark(ITextMark *mark)
{
    Q_UNUSED(mark)
    BaseTextDocumentLayout *documentLayout =
        qobject_cast<BaseTextDocumentLayout*>(document->documentLayout());
    QTC_ASSERT(documentLayout, return);
    documentLayout->requestUpdate();
}

} // namespace Internal
} // namespace TextEditor

CodeFormatterData::~CodeFormatterData()
{
}

TextBlockUserData::~TextBlockUserData()
{
    TextMarks marks = m_marks;
    m_marks.clear();
    foreach (ITextMark *mrk, marks) {
        mrk->removedFromEditor();
    }

    if (m_codeFormatterData)
        delete m_codeFormatterData;
}

int TextBlockUserData::braceDepthDelta() const
{
    int delta = 0;
    for (int i = 0; i < m_parentheses.size(); ++i) {
        switch (m_parentheses.at(i).chr.unicode()) {
        case '{': case '+': case '[': ++delta; break;
        case '}': case '-': case ']': --delta; break;
        default: break;
        }
    }
    return delta;
}

TextBlockUserData::MatchType TextBlockUserData::checkOpenParenthesis(QTextCursor *cursor, QChar c)
{
    QTextBlock block = cursor->block();
    if (!BaseTextDocumentLayout::hasParentheses(block) || BaseTextDocumentLayout::ifdefedOut(block))
        return NoMatch;

    Parentheses parenList = BaseTextDocumentLayout::parentheses(block);
    Parenthesis openParen, closedParen;
    QTextBlock closedParenParag = block;

    const int cursorPos = cursor->position() - closedParenParag.position();
    int i = 0;
    int ignore = 0;
    bool foundOpen = false;
    for (;;) {
        if (!foundOpen) {
            if (i >= parenList.count())
                return NoMatch;
            openParen = parenList.at(i);
            if (openParen.pos != cursorPos) {
                ++i;
                continue;
            } else {
                foundOpen = true;
                ++i;
            }
        }

        if (i >= parenList.count()) {
            for (;;) {
                closedParenParag = closedParenParag.next();
                if (!closedParenParag.isValid())
                    return NoMatch;
                if (BaseTextDocumentLayout::hasParentheses(closedParenParag)
                    && !BaseTextDocumentLayout::ifdefedOut(closedParenParag)) {
                    parenList = BaseTextDocumentLayout::parentheses(closedParenParag);
                    break;
                }
            }
            i = 0;
        }

        closedParen = parenList.at(i);
        if (closedParen.type == Parenthesis::Opened) {
            ignore++;
            ++i;
            continue;
        } else {
            if (ignore > 0) {
                ignore--;
                ++i;
                continue;
            }

            cursor->clearSelection();
            cursor->setPosition(closedParenParag.position() + closedParen.pos + 1, QTextCursor::KeepAnchor);

            if ((c == QLatin1Char('{') && closedParen.chr != QLatin1Char('}'))
                || (c == QLatin1Char('(') && closedParen.chr != QLatin1Char(')'))
                || (c == QLatin1Char('[') && closedParen.chr != QLatin1Char(']'))
                || (c == QLatin1Char('+') && closedParen.chr != QLatin1Char('-'))
               )
                return Mismatch;

            return Match;
        }
    }
}

TextBlockUserData::MatchType TextBlockUserData::checkClosedParenthesis(QTextCursor *cursor, QChar c)
{
    QTextBlock block = cursor->block();
    if (!BaseTextDocumentLayout::hasParentheses(block) || BaseTextDocumentLayout::ifdefedOut(block))
        return NoMatch;

    Parentheses parenList = BaseTextDocumentLayout::parentheses(block);
    Parenthesis openParen, closedParen;
    QTextBlock openParenParag = block;

    const int cursorPos = cursor->position() - openParenParag.position();
    int i = parenList.count() - 1;
    int ignore = 0;
    bool foundClosed = false;
    for (;;) {
        if (!foundClosed) {
            if (i < 0)
                return NoMatch;
            closedParen = parenList.at(i);
            if (closedParen.pos != cursorPos - 1) {
                --i;
                continue;
            } else {
                foundClosed = true;
                --i;
            }
        }

        if (i < 0) {
            for (;;) {
                openParenParag = openParenParag.previous();
                if (!openParenParag.isValid())
                    return NoMatch;

                if (BaseTextDocumentLayout::hasParentheses(openParenParag)
                    && !BaseTextDocumentLayout::ifdefedOut(openParenParag)) {
                    parenList = BaseTextDocumentLayout::parentheses(openParenParag);
                    break;
                }
            }
            i = parenList.count() - 1;
        }

        openParen = parenList.at(i);
        if (openParen.type == Parenthesis::Closed) {
            ignore++;
            --i;
            continue;
        } else {
            if (ignore > 0) {
                ignore--;
                --i;
                continue;
            }

            cursor->clearSelection();
            cursor->setPosition(openParenParag.position() + openParen.pos, QTextCursor::KeepAnchor);

            if ((c == QLatin1Char('}') && openParen.chr != QLatin1Char('{'))    ||
                 (c == QLatin1Char(')') && openParen.chr != QLatin1Char('('))   ||
                 (c == QLatin1Char(']') && openParen.chr != QLatin1Char('['))   ||
                 (c == QLatin1Char('-') && openParen.chr != QLatin1Char('+')))
                return Mismatch;

            return Match;
        }
    }
}

bool TextBlockUserData::findPreviousOpenParenthesis(QTextCursor *cursor, bool select, bool onlyInCurrentBlock)
{
    QTextBlock block = cursor->block();
    int position = cursor->position();
    int ignore = 0;
    while (block.isValid()) {
        Parentheses parenList = BaseTextDocumentLayout::parentheses(block);
        if (!parenList.isEmpty() && !BaseTextDocumentLayout::ifdefedOut(block)) {
            for (int i = parenList.count()-1; i >= 0; --i) {
                Parenthesis paren = parenList.at(i);
                if (block == cursor->block() &&
                    (position - block.position() <= paren.pos + (paren.type == Parenthesis::Closed ? 1 : 0)))
                        continue;
                if (paren.type == Parenthesis::Closed) {
                    ++ignore;
                } else if (ignore > 0) {
                    --ignore;
                } else {
                    cursor->setPosition(block.position() + paren.pos, select ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
                    return true;
                }
            }
        }
        if (onlyInCurrentBlock)
            return false;
        block = block.previous();
    }
    return false;
}

bool TextBlockUserData::findPreviousBlockOpenParenthesis(QTextCursor *cursor, bool checkStartPosition)
{
    QTextBlock block = cursor->block();
    int position = cursor->position();
    int ignore = 0;
    while (block.isValid()) {
        Parentheses parenList = BaseTextDocumentLayout::parentheses(block);
        if (!parenList.isEmpty() && !BaseTextDocumentLayout::ifdefedOut(block)) {
            for (int i = parenList.count()-1; i >= 0; --i) {
                Parenthesis paren = parenList.at(i);
                if (paren.chr != QLatin1Char('{') && paren.chr != QLatin1Char('}')
                    && paren.chr != QLatin1Char('+') && paren.chr != QLatin1Char('-')
                    && paren.chr != QLatin1Char('[') && paren.chr != QLatin1Char(']'))
                    continue;
                if (block == cursor->block()) {
                    if (position - block.position() <= paren.pos + (paren.type == Parenthesis::Closed ? 1 : 0))
                        continue;
                    if (checkStartPosition && paren.type == Parenthesis::Opened && paren.pos== cursor->position()) {
                        return true;
                    }
                }
                if (paren.type == Parenthesis::Closed) {
                    ++ignore;
                } else if (ignore > 0) {
                    --ignore;
                } else {
                    cursor->setPosition(block.position() + paren.pos);
                    return true;
                }
            }
        }
        block = block.previous();
    }
    return false;
}

bool TextBlockUserData::findNextClosingParenthesis(QTextCursor *cursor, bool select)
{
    QTextBlock block = cursor->block();
    int position = cursor->position();
    int ignore = 0;
    while (block.isValid()) {
        Parentheses parenList = BaseTextDocumentLayout::parentheses(block);
        if (!parenList.isEmpty() && !BaseTextDocumentLayout::ifdefedOut(block)) {
            for (int i = 0; i < parenList.count(); ++i) {
                Parenthesis paren = parenList.at(i);
                if (block == cursor->block() &&
                    (position - block.position() > paren.pos - (paren.type == Parenthesis::Opened ? 1 : 0)))
                    continue;
                if (paren.type == Parenthesis::Opened) {
                    ++ignore;
                } else if (ignore > 0) {
                    --ignore;
                } else {
                    cursor->setPosition(block.position() + paren.pos+1, select ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
                    return true;
                }
            }
        }
        block = block.next();
    }
    return false;
}

bool TextBlockUserData::findNextBlockClosingParenthesis(QTextCursor *cursor)
{
    QTextBlock block = cursor->block();
    int position = cursor->position();
    int ignore = 0;
    while (block.isValid()) {
        Parentheses parenList = BaseTextDocumentLayout::parentheses(block);
        if (!parenList.isEmpty() && !BaseTextDocumentLayout::ifdefedOut(block)) {
            for (int i = 0; i < parenList.count(); ++i) {
                Parenthesis paren = parenList.at(i);
                if (paren.chr != QLatin1Char('{') && paren.chr != QLatin1Char('}')
                    && paren.chr != QLatin1Char('+') && paren.chr != QLatin1Char('-')
                    && paren.chr != QLatin1Char('[') && paren.chr != QLatin1Char(']'))
                    continue;
                if (block == cursor->block() &&
                    (position - block.position() > paren.pos - (paren.type == Parenthesis::Opened ? 1 : 0)))
                    continue;
                if (paren.type == Parenthesis::Opened) {
                    ++ignore;
                } else if (ignore > 0) {
                    --ignore;
                } else {
                    cursor->setPosition(block.position() + paren.pos+1);
                    return true;
                }
            }
        }
        block = block.next();
    }
    return false;
}

TextBlockUserData::MatchType TextBlockUserData::matchCursorBackward(QTextCursor *cursor)
{
    cursor->clearSelection();
    const QTextBlock block = cursor->block();

    if (!BaseTextDocumentLayout::hasParentheses(block) || BaseTextDocumentLayout::ifdefedOut(block))
        return NoMatch;

    const int relPos = cursor->position() - block.position();

    Parentheses parentheses = BaseTextDocumentLayout::parentheses(block);
    const Parentheses::const_iterator cend = parentheses.constEnd();
    for (Parentheses::const_iterator it = parentheses.constBegin();it != cend; ++it) {
        const Parenthesis &paren = *it;
        if (paren.pos == relPos - 1
            && paren.type == Parenthesis::Closed) {
            return checkClosedParenthesis(cursor, paren.chr);
        }
    }
    return NoMatch;
}

TextBlockUserData::MatchType TextBlockUserData::matchCursorForward(QTextCursor *cursor)
{
    cursor->clearSelection();
    const QTextBlock block = cursor->block();

    if (!BaseTextDocumentLayout::hasParentheses(block) || BaseTextDocumentLayout::ifdefedOut(block))
        return NoMatch;

    const int relPos = cursor->position() - block.position();

    Parentheses parentheses = BaseTextDocumentLayout::parentheses(block);
    const Parentheses::const_iterator cend = parentheses.constEnd();
    for (Parentheses::const_iterator it = parentheses.constBegin();it != cend; ++it) {
        const Parenthesis &paren = *it;
        if (paren.pos == relPos
            && paren.type == Parenthesis::Opened) {
            return checkOpenParenthesis(cursor, paren.chr);
        }
    }
    return NoMatch;
}

void TextBlockUserData::setCodeFormatterData(CodeFormatterData *data)
{
    if (m_codeFormatterData)
        delete m_codeFormatterData;

    m_codeFormatterData = data;
}

void TextBlockUserData::addMark(ITextMark *mark)
{
    int i = 0;
    for ( ; i < m_marks.size(); ++i) {
        if (mark->priority() < m_marks.at(i)->priority())
            break;
    }
    m_marks.insert(i, mark);
}


BaseTextDocumentLayout::BaseTextDocumentLayout(QTextDocument *doc)
    : QPlainTextDocumentLayout(doc),
      lastSaveRevision(0),
      hasMarks(false),
      maxMarkWidthFactor(1.0),
      m_requiredWidth(0),
      m_documentMarker(new Internal::DocumentMarker(doc))
{

}

BaseTextDocumentLayout::~BaseTextDocumentLayout()
{
    documentClosing();
}

void BaseTextDocumentLayout::setParentheses(const QTextBlock &block, const Parentheses &parentheses)
{
    if (parentheses.isEmpty()) {
        if (TextBlockUserData *userData = testUserData(block))
            userData->clearParentheses();
    } else {
        userData(block)->setParentheses(parentheses);
    }
}

Parentheses BaseTextDocumentLayout::parentheses(const QTextBlock &block)
{
    if (TextBlockUserData *userData = testUserData(block))
        return userData->parentheses();
    return Parentheses();
}

bool BaseTextDocumentLayout::hasParentheses(const QTextBlock &block)
{
    if (TextBlockUserData *userData = testUserData(block))
        return userData->hasParentheses();
    return false;
}

bool BaseTextDocumentLayout::setIfdefedOut(const QTextBlock &block)
{
    return userData(block)->setIfdefedOut();
}

bool BaseTextDocumentLayout::clearIfdefedOut(const QTextBlock &block)
{
    if (TextBlockUserData *userData = testUserData(block))
        return userData->clearIfdefedOut();
    return false;
}

bool BaseTextDocumentLayout::ifdefedOut(const QTextBlock &block)
{
    if (TextBlockUserData *userData = testUserData(block))
        return userData->ifdefedOut();
    return false;
}

int BaseTextDocumentLayout::braceDepthDelta(const QTextBlock &block)
{
    if (TextBlockUserData *userData = testUserData(block))
        return userData->braceDepthDelta();
    return 0;
}

int BaseTextDocumentLayout::braceDepth(const QTextBlock &block)
{
    int state = block.userState();
    if (state == -1)
        return 0;
    return state >> 8;
}

void BaseTextDocumentLayout::setBraceDepth(QTextBlock &block, int depth)
{
    int state = block.userState();
    if (state == -1)
        state = 0;
    state = state & 0xff;
    block.setUserState((depth << 8) | state);
}

void BaseTextDocumentLayout::changeBraceDepth(QTextBlock &block, int delta)
{
    if (delta)
        setBraceDepth(block, braceDepth(block) + delta);
}

void BaseTextDocumentLayout::setLexerState(const QTextBlock &block, int state)
{
    if (state == 0) {
        if (TextBlockUserData *userData = testUserData(block))
            userData->setLexerState(0);
    } else {
        userData(block)->setLexerState(qMax(0,state));
    }
}

int BaseTextDocumentLayout::lexerState(const QTextBlock &block)
{
    if (TextBlockUserData *userData = testUserData(block))
        return userData->lexerState();
    return 0;
}

void BaseTextDocumentLayout::setFoldingIndent(const QTextBlock &block, int indent)
{
    if (indent == 0) {
        if (TextBlockUserData *userData = testUserData(block))
            userData->setFoldingIndent(0);
    } else {
        userData(block)->setFoldingIndent(qMax(0,indent));
    }
}

int BaseTextDocumentLayout::foldingIndent(const QTextBlock &block)
{
    if (TextBlockUserData *userData = testUserData(block))
        return userData->foldingIndent();
    return 0;
}

void BaseTextDocumentLayout::changeFoldingIndent(QTextBlock &block, int delta)
{
    if (delta)
        setFoldingIndent(block, foldingIndent(block) + delta);
}

bool BaseTextDocumentLayout::canFold(const QTextBlock &block)
{
    return (block.next().isValid() && foldingIndent(block.next()) > foldingIndent(block));
}

bool BaseTextDocumentLayout::isFolded(const QTextBlock &block)
{
    if (TextBlockUserData *userData = testUserData(block))
        return userData->folded();
    return false;
}

void BaseTextDocumentLayout::setFolded(const QTextBlock &block, bool folded)
{
    if (folded)
        userData(block)->setFolded(true);
    else {
        if (TextBlockUserData *userData = testUserData(block))
            return userData->setFolded(false);
    }
}

ITextMarkable *BaseTextDocumentLayout::markableInterface()
{
    return m_documentMarker;
}

void BaseTextDocumentLayout::doFoldOrUnfold(const QTextBlock& block, bool unfold)
{
    if (!canFold(block))
        return;
    QTextBlock b = block.next();

    int indent = foldingIndent(block);
    while (b.isValid() && foldingIndent(b) > indent && (unfold || b.next().isValid())) {
        b.setVisible(unfold);
        b.setLineCount(unfold? qMax(1, b.layout()->lineCount()) : 0);
        if (unfold) { // do not unfold folded sub-blocks
            if (isFolded(b) && b.next().isValid()) {
                int jndent = foldingIndent(b);
                b = b.next();
                while (b.isValid() && foldingIndent(b) > jndent)
                    b = b.next();
                continue;
            }
        }
        b = b.next();
    }
    setFolded(block, !unfold);
}

void BaseTextDocumentLayout::setRequiredWidth(int width)
{
    int oldw = m_requiredWidth;
    m_requiredWidth = width;
    int dw = QPlainTextDocumentLayout::documentSize().width();
    if (oldw > dw || width > dw)
        emitDocumentSizeChanged();
}


QSizeF BaseTextDocumentLayout::documentSize() const
{
    QSizeF size = QPlainTextDocumentLayout::documentSize();
    size.setWidth(qMax((qreal)m_requiredWidth, size.width()));
    return size;
}

void BaseTextDocumentLayout::documentClosing()
{
    QTextBlock block = document()->begin();
    while (block.isValid()) {
        if (TextBlockUserData *data = static_cast<TextBlockUserData *>(block.userData()))
            data->documentClosing();
        block = block.next();
    }
}


void BaseTextDocumentLayout::updateMarksLineNumber()
{
    QTextBlock block = document()->begin();
    int blockNumber = 0;
    while (block.isValid()) {
        if (const TextBlockUserData *userData = testUserData(block))
            foreach (ITextMark *mrk, userData->marks()) {
                mrk->updateLineNumber(blockNumber + 1);
                QTC_CHECK(mrk->lineNumber() == blockNumber +1);
            }
        block = block.next();
        ++blockNumber;
    }
}

void BaseTextDocumentLayout::updateMarksBlock(const QTextBlock &block)
{
    if (const TextBlockUserData *userData = testUserData(block))
        foreach (ITextMark *mrk, userData->marks())
            mrk->updateBlock(block);
}

BaseTextDocumentLayout::FoldValidator::FoldValidator()
    : m_layout(0)
    , m_requestDocUpdate(false)
    , m_insideFold(0)
{}

void BaseTextDocumentLayout::FoldValidator::setup(BaseTextDocumentLayout *layout)
{
    m_layout = layout;
}

void BaseTextDocumentLayout::FoldValidator::reset()
{
    m_insideFold = 0;
    m_requestDocUpdate = false;
}

void BaseTextDocumentLayout::FoldValidator::process(QTextBlock block)
{
    if (!m_layout)
        return;

    const QTextBlock &previous = block.previous();
    if (!previous.isValid())
        return;

    if ((BaseTextDocumentLayout::isFolded(previous)
            && !BaseTextDocumentLayout::canFold(previous))
            || (!BaseTextDocumentLayout::isFolded(previous)
                && BaseTextDocumentLayout::canFold(previous)
                && !block.isVisible())) {
        BaseTextDocumentLayout::setFolded(previous, !BaseTextDocumentLayout::isFolded(previous));
    }

    if (BaseTextDocumentLayout::isFolded(previous) && !m_insideFold)
        m_insideFold = BaseTextDocumentLayout::foldingIndent(block);

    bool toggleVisibility = false;
    if (m_insideFold) {
        if (BaseTextDocumentLayout::foldingIndent(block) >= m_insideFold) {
            if (block.isVisible())
                toggleVisibility = true;
        } else {
            m_insideFold = 0;
            if (!block.isVisible())
                toggleVisibility = true;
        }
    } else if (!block.isVisible()) {
        toggleVisibility = true;
    }

    if (toggleVisibility) {
        block.setVisible(!block.isVisible());
        block.setLineCount(block.isVisible() ? qMax(1, block.layout()->lineCount()) : 0);
        m_requestDocUpdate = true;
    }
}

void BaseTextDocumentLayout::FoldValidator::finalize()
{
    if (m_requestDocUpdate && m_layout) {
        m_layout->requestUpdate();
        m_layout->emitDocumentSizeChanged();
    }
}

#include "basetextdocumentlayout.moc"
