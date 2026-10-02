export module engine.gui.w3d.dialog_layout;
import std;
import engine.gui.w3d.dialog_template;
export import engine.gui.w3d.dialog_input;

export namespace engine::gui::w3d
{
// The host supplies the dialog-unit design size. Center the dialog and scale
// its authored control rectangles using the same transform as hit testing.
HitRect LayoutControlAt(const DialogControlDefinition& control,float left,float top,
    float width,float height,float design_width,float design_height) {
    if(width<=0 || height<=0 || design_width<=0 || design_height<=0) return {};
    const float sx=width/design_width,sy=height/design_height;
    const float x=static_cast<int>(left+control.rect.x*sx),y=static_cast<int>(top+control.rect.y*sy);
    return {x,y,x+static_cast<int>(control.rect.width*sx),y+static_cast<int>(control.rect.height*sy)};
}
HitRect LayoutControl(const DialogDefinition& dialog,const DialogControlDefinition& control,
    float width,float height,float design_width,float design_height) {
    if(width<=0 || height<=0 || design_width<=0 || design_height<=0) return {};
    const float sx=width/design_width,sy=height/design_height;
    const float left=static_cast<int>((width-static_cast<int>(dialog.rect.width*sx))*0.5f);
    const float top=static_cast<int>((height-static_cast<int>(dialog.rect.height*sy))*0.5f);
    return LayoutControlAt(control,left,top,width,height,design_width,design_height);
}
}
