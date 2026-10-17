import java.io.File;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicBoolean;

/** Session-only Samsung default cursor. Never writes Android settings.
 * Stop marker, process shutdown, or a bounded lease restores the exact default. */
public final class PointerLease {
 public static void main(String[] args) throws Exception {
  if(args.length!=2 || !args[0].matches("[1-9][0-9]*") || !args[1].matches("dovedex-[0-9]+"))
   throw new IllegalArgumentException("Expected display ID and session token");
  int display=Integer.parseInt(args[0]);
  Object binder=Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"input");
  Class<?> api=Class.forName("android.hardware.input.IInputManager");
  final Object manager=Class.forName("android.hardware.input.IInputManager$Stub").getMethod("asInterface",Class.forName("android.os.IBinder")).invoke(null,binder);
  int current=(Integer)api.getMethod("getDisplayIdForPointerIcon").invoke(manager);
  if(current!=display){System.out.println("POINTER_ROUTING_UNCHANGED current="+current);return;}
  final Class<?> pointer=Class.forName("android.view.PointerIcon");
  final Method set=api.getMethod("setDefaultPointerIcon",int.class,pointer,boolean.class);
  final Object original=api.getMethod("getDefaultPointerIcon").invoke(manager);
  final int tool=(Integer)api.getMethod("getToolTypeForDefaultPointerIcon").invoke(manager);
  final boolean changed=(Boolean)api.getMethod("isDefaultPointerIconChanged").invoke(manager);
  final AtomicBoolean restored=new AtomicBoolean();
  final File stop=new File("/data/local/tmp/"+args[1]+".stop");
  if(stop.exists())throw new IllegalStateException("Stop marker already exists");
  Runnable restore=()->{
   if(!restored.compareAndSet(false,true))return;
   try {
    // Samsung ignores a null default without force=true. Explicitly clearing
    // the mouse override restores the original tool=0/changed=false/null state.
    set.invoke(manager,original==null?3:tool,original,true);
    int restoredTool=(Integer)api.getMethod("getToolTypeForDefaultPointerIcon").invoke(manager);
    boolean restoredChanged=(Boolean)api.getMethod("isDefaultPointerIconChanged").invoke(manager);
    Object restoredIcon=api.getMethod("getDefaultPointerIcon").invoke(manager);
    System.out.println("POINTER_DEFAULT_RESTORED tool="+restoredTool+" changed="+restoredChanged+" icon="+restoredIcon);
    if(restoredTool!=tool||restoredChanged!=changed||(restoredIcon==null)!=(original==null))
     System.err.println("POINTER_RESTORE_STATE_MISMATCH");
    stop.delete();
   } catch(Exception e){System.err.println("POINTER_RESTORE_FAILED "+e);}
  };
  Runtime.getRuntime().addShutdownHook(new Thread(restore,"pointer-restore"));
  try {
   android.graphics.Bitmap bitmap=android.graphics.Bitmap.createBitmap(2,2,android.graphics.Bitmap.Config.ARGB_8888);
   Object transparent=pointer.getMethod("create",android.graphics.Bitmap.class,float.class,float.class).invoke(null,bitmap,0f,0f);
   set.invoke(manager,3,transparent,true);
   System.out.println("POINTER_DEFAULT_TRANSPARENT display="+display);
   System.out.flush();
   long deadline=System.nanoTime()+1790L*1000000000L;
   while(!stop.exists() && System.nanoTime()<deadline)Thread.sleep(250);
  } finally {restore.run();}
 }
}
