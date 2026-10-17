from pathlib import Path
import shutil
q=Path('/mnt/c/vscode/HPlapdock/doveos_v2/gpu/rgb/xephyr')
b=Path('/home/hakami55/doveos-v2-build/xephyr-source')
s=(b/'source/glamor/glamor_copy.c').read_text(encoding='utf-8')
# Use the native integer texture-copy operation for simple GPU CopyArea.
# Shader copying remains the fallback for other ROPs and CopyPlane.
needle='    glamor_make_current(glamor_priv);\n\n    if (gc && !glamor_set_planemask(gc->depth, gc->planemask))'
assert s.count(needle)==2
hook=r"""    glamor_make_current(glamor_priv);

    if (getenv("DOVE_COPY_NATIVE") && !bitplane && src_pixmap != dst_pixmap &&
        (!gc || (gc->alu == GXcopy && glamor_pm_is_solid(gc->depth, gc->planemask)))) {
        int sx, sy, tx, ty;
        glamor_get_drawable_deltas(src, src_pixmap, &sx, &sy);
        glamor_get_drawable_deltas(dst, dst_pixmap, &tx, &ty);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        glamor_pixmap_loop(src_priv, src_box_index) {
            BoxPtr sb = glamor_pixmap_box_at(src_priv, src_box_index);
            glamor_pixmap_fbo *sf = glamor_pixmap_fbo_at(src_priv, src_box_index);
            glamor_pixmap_loop(dst_priv, dst_box_index) {
                BoxPtr db = glamor_pixmap_box_at(dst_priv, dst_box_index);
                glamor_pixmap_fbo *df = glamor_pixmap_fbo_at(dst_priv, dst_box_index);
                glBindFramebuffer(GL_FRAMEBUFFER, sf->fb);
                glBindTexture(GL_TEXTURE_2D, df->tex);
                for (int i = 0; i < nbox; ++i) {
                    int x1=max(box[i].x1,max(sb->x1-dx-sx,db->x1-tx));
                    int y1=max(box[i].y1,max(sb->y1-dy-sy,db->y1-ty));
                    int x2=min(box[i].x2,min(sb->x2-dx-sx,db->x2-tx));
                    int y2=min(box[i].y2,min(sb->y2-dy-sy,db->y2-ty));
                    if(x1<x2 && y1<y2)
                        glCopyTexSubImage2D(GL_TEXTURE_2D,0,
                            x1+tx-db->x1,y1+ty-db->y1,
                            x1+dx+sx-sb->x1,y1+dy+sy-sb->y1,x2-x1,y2-y1);
                }
            }
        }
        if(glGetError()==GL_NO_ERROR) return TRUE;
    }

    if (gc && !glamor_set_planemask(gc->depth, gc->planemask))"""
s=s.replace(needle,hook,1)
(q/'glamor_copy.c').write_bytes(s.encode('utf-8'))
(q/'.libs').mkdir(exist_ok=True)
shutil.copyfile(b/'build/glamor/.libs/libglamor.a',q/'.libs/libglamor.a')
shutil.copyfile(b/'build/glamor/libglamor.la',q/'libglamor.la')
print('PRIVATE_COPY_SOURCE_READY')
