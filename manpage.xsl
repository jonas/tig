<!-- manpage.xsl: various fixups to docbook -> manpage output
  -
  - For now converts it adds a newline after preformatted text enclosed
  - in screen.
  -
  -->

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
<xsl:template match="screen">
	<xsl:text>.sp&#10;.nf&#10;</xsl:text>
	<xsl:apply-templates/>
	<xsl:text>&#10;.fi&#10;.sp&#10;</xsl:text>
</xsl:template>

<xsl:template match="simpara">
  <xsl:variable name="content">
    <xsl:apply-templates/>
  </xsl:variable>
  <xsl:value-of select="normalize-space($content)"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:text>.sp&#10;</xsl:text>
</xsl:template>

<xsl:template match="address|literallayout|programlisting|screen|synopsis">
  <!-- * Yes, address and synopsis are verbatim environments. -->

  <xsl:choose>
    <!-- * Check to see if this verbatim item is within a parent element that -->
    <!-- * allows mixed content. -->
    <!-- * -->
    <!-- * If it is within a mixed-content parent, then a line space is -->
    <!-- * already added before it by the mixed-block template, so we don't -->
    <!-- * need to add one here. -->
    <!-- * -->
    <!-- * If it is not within a mixed-content parent, then we need to add a -->
    <!-- * line space before it. -->
    <xsl:when test="parent::caption|parent::entry|parent::para|
                    parent::td|parent::th" /> <!-- do nothing -->
    <xsl:otherwise>
  <xsl:text>&#10;</xsl:text>
      <xsl:text>.sp&#10;</xsl:text>
    </xsl:otherwise>
  </xsl:choose>
  <xsl:text>.nf&#10;</xsl:text>
  <xsl:apply-templates/>
  <xsl:text>&#10;</xsl:text>
  <xsl:text>&#10;&#10;.fi&#10;</xsl:text>
  <!-- * if first following sibling node of this verbatim -->
  <!-- * environment is a text node, output a line of space before it -->
  <xsl:if test="following-sibling::node()[1][name(.) = '']">
    <xsl:text>&#10;</xsl:text>
    <xsl:text>.sp&#10;</xsl:text>
  </xsl:if>
</xsl:template>
</xsl:stylesheet>
