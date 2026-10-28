<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.10.2" name="cliff" tilewidth="16" tileheight="16" tilecount="2" columns="2">
 <image source="cliff-missing.png" width="32" height="16"/>
 <tile id="0">
  <properties>
   <property name="walkable" type="bool" value="false"/>
   <property name="terrainTag" type="int" value="3"/>
  </properties>
  <animation>
   <frame tileid="0" duration="80"/>
   <frame tileid="1" duration="120"/>
  </animation>
  <objectgroup>
   <object id="1" x="0" y="0" width="8" height="16"/>
  </objectgroup>
 </tile>
 <wangsets>
  <wangset name="cliff-edge" type="edge" tile="0">
   <wangcolor name="cliff" color="#ff0000" tile="0" probability="1"/>
   <wangtile tileid="0" wangid="1,0,0,0,0,0,0,0"/>
  </wangset>
 </wangsets>
</tileset>
