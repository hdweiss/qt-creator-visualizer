source("../../shared/qtcreator.py")

# This tests for QTCREATORBUG-5757

# Results can differ from actual size on disk (different line endings on Windows)
def charactersInFile(filename):
    f = open(filename,"r")
    content = f.read()
    f.close()
    return len(content)

def main():
    filesAndEditors = {srcPath + "/creator/README" : "TextEditor::PlainTextEditorWidget",
                       srcPath + "/creator/qtcreator.pri" : "Qt4ProjectManager::Internal::ProFileEditorWidget",
                       srcPath + "/creator/doc/snippets/qml/list-of-transitions.qml" : "QmlJSEditor::QmlJSTextEditorWidget"}
    for currentFile in filesAndEditors:
        if not neededFilePresent(currentFile):
            return

    startApplication("qtcreator" + SettingsPath)
    for currentFile in filesAndEditors:
        test.log("Opening file %s" % currentFile)
        size = charactersInFile(currentFile)
        invokeMenuItem("File", "Open File or Project...")
        selectFromFileDialog(currentFile)
        editor = waitForObject("{type='%s' unnamed='1' visible='1' window=':Qt Creator_Core::Internal::MainWindow'}"
                               % filesAndEditors[currentFile], 20000)
        JIRA.performWorkaroundIfStillOpen(6918, JIRA.Bug.CREATOR, editor)
        for key in ["<Up>", "<Down>", "<Left>", "<Right>"]:
            test.log("Selecting everything")
            invokeMenuItem("Edit", "Select All")
            waitFor("editor.textCursor().hasSelection()", 1000)
            test.compare(editor.textCursor().selectionStart(), 0)
            test.compare(editor.textCursor().selectionEnd(), size)
            test.compare(editor.textCursor().position(), size)
            test.log("Pressing key %s" % key)
            type(editor, key)
            if key == "<Up>":
                test.compare(editor.textCursor().selectionStart(), editor.textCursor().selectionEnd())
            else:
                pos = size
                if key == "<Left>":
                    pos -= 1
                test.compare(editor.textCursor().selectionStart(), pos)
                test.compare(editor.textCursor().selectionEnd(), pos)
                test.compare(editor.textCursor().position(), pos)
    invokeMenuItem("File", "Exit")
    waitForCleanShutdown()
