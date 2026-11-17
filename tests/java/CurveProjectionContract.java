package app.toolmoa.ppm;
import java.time.LocalDateTime;
import java.util.*;
import org.mpxj.*;

/** Projection contract: generated objects exercise boundaries unavailable in binary fixtures. */
public final class CurveProjectionContract {
    static int checks;
    static void check(boolean ok,String label){++checks;if(!ok)throw new AssertionError(label);}
    static ProjectFile project(){
        ProjectFile p=new ProjectFile();ProjectCalendar calendar=p.addDefaultBaseCalendar();
        Task t=p.addTask();t.setCalendar(calendar);t.setName("Curve");t.setStart(LocalDateTime.of(2026,11,2,9,0));
        t.setFinish(LocalDateTime.of(2026,11,2,17,0));t.setDuration(Duration.getInstance(480,TimeUnit.MINUTES));
        Resource r=p.addResource();r.setName("R");r.setType(ResourceType.WORK);
        ResourceAssignment a=t.addResourceAssignment(r);a.setWorkContour(WorkContour.CONTOURED);
        a.setStart(t.getStart());a.setFinish(t.getFinish());return p;
    }
    static TimephasedWork row(int hour,double minutes){
        TimephasedWork r=new TimephasedWork();r.setStart(LocalDateTime.of(2026,11,2,hour,0));r.setFinish(r.getStart().plusHours(1));
        r.setTotalAmount(Duration.getInstance(minutes,TimeUnit.MINUTES));r.setAmountPerHour(Duration.getInstance(minutes,TimeUnit.MINUTES));return r;
    }
    static ResourceAssignment assignment(ProjectFile p){return p.getResourceAssignments().get(0);}
    static void rows(ProjectFile p,double work,TimephasedWork... rows){ResourceAssignment a=assignment(p);a.setWork(Duration.getInstance(work,TimeUnit.MINUTES));a.set(AssignmentField.RAW_TIMEPHASED_REMAINING_REGULAR_WORK,new ArrayList<>(Arrays.asList(rows)));}
    static void rejects(ProjectFile p,String label){try{new MppBridge.CurveProjection().prepare(p);}catch(IllegalArgumentException e){check(true,label);return;}throw new AssertionError(label);}
    public static void main(String[] args){
        ProjectFile p=project();TimephasedWork first=row(9,1.005);rows(p,3.01,first,row(10,2.005));
        MppBridge.CurveProjection c=new MppBridge.CurveProjection();c.prepare(p);
        List<TimephasedWork> result=assignment(p).getRawTimephasedRemainingRegularWork();
        check(Math.round(result.get(0).getTotalAmount().getDuration()*100)==101,"stable largest remainder");
        check(Math.round(result.get(1).getTotalAmount().getDuration()*100)==200,"exact total quantum");
        check(first.getTotalAmount().getDuration()==1.005,"owned rows");check(c.rows==2&&c.work==2,"rounding metadata");
        p=project();TimephasedWork z=row(9,0);z.setTotalAmount(null);rows(p,0,z);c=new MppBridge.CurveProjection();c.prepare(p);
        check(assignment(p).getRawTimephasedRemainingRegularWork().get(0).getTotalAmount().getDuration()==0,"explicit zero gap");
        p=project();rows(p,1,row(9,1));assignment(p).setActualWork(Duration.getInstance(0.5,TimeUnit.MINUTES));c=new MppBridge.CurveProjection();c.prepare(p);
        check(c.actual==1&&c.rows==0&&assignment(p).getRawTimephasedRemainingRegularWork().isEmpty(),"actual data excluded");
        check(assignment(p).getActualWork().getDuration()==0.5,"actual scalar unchanged");
        p=project();rows(p,1,row(9,1));assignment(p).setRemainingOvertimeWork(Duration.getInstance(1,TimeUnit.MINUTES));c=new MppBridge.CurveProjection();c.prepare(p);check(c.overtime==1&&c.rows==0,"overtime excluded");
        p=project();assignment(p).setWork(Duration.getInstance(1,TimeUnit.MINUTES));c=new MppBridge.CurveProjection();c.prepare(p);check(c.missing==1,"missing source data not generated");
        p=project();TimephasedWork sub=row(9,1);sub.setStart(sub.getStart().plusSeconds(5));sub.setFinish(sub.getStart().plusSeconds(5));rows(p,1,sub);rejects(p,"collapsed positive subminute interval");
        p=project();rows(p,2,row(9,1));rejects(p,"source total mismatch");
        p=project();TimephasedWork missing=row(9,1);missing.setTotalAmount(null);rows(p,0,missing);rejects(p,"unknown amount not zero");
        p=project();rows(p,1,row(9,1),row(9,0));rejects(p,"overlapping intervals");
        p=project();rows(p,1,row(9,-1));rejects(p,"negative work");
        p=project();TimephasedWork[] many=new TimephasedWork[1025];Arrays.fill(many,row(9,0));rows(p,0,many);rejects(p,"assignment row budget");
        p=project();TimephasedWork shifted=row(9,1);shifted.setStart(shifted.getStart().plusSeconds(5));shifted.setFinish(shifted.getFinish().plusSeconds(5));rows(p,1,shifted);c=new MppBridge.CurveProjection();c.prepare(p);check(c.boundaries==2,"boundary rounding disclosed");
        System.out.println("PASS "+checks+" Java curve projection checks: quantization/zeros/actuals/overtime/missing/order/limits/boundaries");
    }
}
