pragma ComponentBehavior: Bound
import QtQuick
import QMarkdown 1.1
import Holonight.Core

MarkdownView {
    id: document
    property list<url> authorizedRoots: []
    property font bodyFont: Qt.font({
        family: HolonightTheme.uiFont,
        pointSize: HolonightTheme.bodySize
    })
    objectName: "quickLookMarkdownView"
    resourcePolicy: MarkdownResourcePolicy {
        allowedFileRoots: document.authorizedRoots
        allowedHttpsOrigins: []
        allowQrc: false
    }
    style: MarkdownStyle {
        preset: HoloniightPalette.surface.hslLightness < 0.5 ? MarkdownStyle.PolishedDark : MarkdownStyle.PolishedLight
        bodyFont: document.bodyFont
        bodyColor: HoloniightPalette.textPrimary
        headings.h1.font: Qt.font({
            family: HolonightTheme.uiFont,
            pointSize: HolonightTheme.headingSize,
            bold: true
        })
        headings.h2.font: Qt.font({
            family: HolonightTheme.uiFont,
            pointSize: HolonightTheme.subheadingSize,
            bold: true
        })
        headings.h3.font: Qt.font({
            family: HolonightTheme.uiFont,
            pointSize: HolonightTheme.bodySize * 1.1,
            bold: true
        })
        headings.h4.font: Qt.font({
            family: HolonightTheme.uiFont,
            pointSize: HolonightTheme.bodySize,
            bold: true
        })
        headings.h5.font: headings.h4.font
        headings.h6.font: headings.h4.font
        headings.h1.color: HoloniightPalette.textPrimary
        headings.h2.color: HoloniightPalette.textPrimary
        headings.h3.color: HoloniightPalette.textPrimary
        headings.h4.color: HoloniightPalette.textPrimary
        headings.h5.color: HoloniightPalette.textPrimary
        headings.h6.color: HoloniightPalette.textPrimary
        codeBlock.font: Qt.font({
            family: HolonightTheme.monospaceFont,
            pointSize: HolonightTheme.monospaceFontSize
        })
        inlineCode.font.family: HolonightTheme.monospaceFont
        codeBlock.color: HoloniightPalette.textPrimary
        inlineCode.backgroundColor: HoloniightPalette.surfaceRaised
        codeBlock.backgroundColor: HoloniightPalette.surfaceElevated
        codeBlock.borderColor: HoloniightPalette.borderSubtle
        headings.h1.separatorColor: HoloniightPalette.borderSubtle
        headings.h2.separatorColor: HoloniightPalette.borderSubtle
        headings.h3.separatorColor: HoloniightPalette.borderSubtle
        headings.h4.separatorColor: HoloniightPalette.borderSubtle
        headings.h5.separatorColor: HoloniightPalette.borderSubtle
        headings.h6.separatorColor: HoloniightPalette.borderSubtle
        linkColor: HoloniightPalette.accentBlue
        quoteRuleColor: HoloniightPalette.borderSubtle
        thematicBreakColor: HoloniightPalette.borderSubtle
    }
}
