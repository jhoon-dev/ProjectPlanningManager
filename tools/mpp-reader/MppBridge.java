package app.toolmoa.ppm;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.time.LocalDateTime;
import java.math.BigDecimal;
import java.math.RoundingMode;
import java.time.temporal.ChronoUnit;
import org.apache.poi.poifs.filesystem.POIFSFileSystem;
import org.mpxj.*;
import org.mpxj.mpp.MPPReader;
import org.mpxj.mspdi.MSPDIWriter;

/** Read-only, one request per process. The original file never has a filesystem path. */
public final class MppBridge {
    static final int INPUT_LIMIT = 32 * 1024 * 1024;
    static final int OUTPUT_LIMIT = 16 * 1024 * 1024;
    static String quote(String s) {
        if (s == null) return "null";
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 32) b.append(String.format(Locale.ROOT, "\\u%04x", (int)c));
            else b.append(c);
        }
        return b.append('"').toString();
    }
    static final class BoundedOutput extends ByteArrayOutputStream {
        @Override public synchronized void write(byte[] b, int offset, int length) {
            if (length > OUTPUT_LIMIT - size()) throw new IllegalStateException("OUTPUT_LIMIT");
            super.write(b, offset, length);
        }
        @Override public synchronized void write(int b) {
            if (size() == OUTPUT_LIMIT) throw new IllegalStateException("OUTPUT_LIMIT");
            super.write(b);
        }
    }
    static final class CurveProjection {
        int assignments, rows, actual, overtime, missing, boundaries, durations, origins, work, progressPlaceholders;
        static double minutes(Duration value, ProjectFile project) {
            if(value==null) return 0;
            double result=value.convertUnits(TimeUnit.MINUTES,project.getProjectProperties()).getDuration();
            if(!Double.isFinite(result)||result<0||result>5000000000.0) throw new IllegalArgumentException("CURVE_WORK_LIMIT");
            return result;
        }
        static LocalDateTime minute(LocalDateTime value) {
            if(value==null) throw new IllegalArgumentException("CURVE_MISSING_DATE");
            LocalDateTime result=value.truncatedTo(ChronoUnit.MINUTES);
            return result.equals(value)?result:result.plusMinutes(1);
        }
        void prepare(ProjectFile project) {
            Map<Task,LocalDateTime> finishes=new LinkedHashMap<>();
            for(ResourceAssignment a:project.getResourceAssignments()) {
                List<TimephasedWork> source=new ArrayList<>(a.getRawTimephasedRemainingRegularWork());
                boolean observed=minutes(a.getActualWork(),project)>0 || minutes(a.getActualOvertimeWork(),project)>0 ||
                    !a.getRawTimephasedActualRegularWork().isEmpty() || !a.getRawTimephasedActualOvertimeWork().isEmpty();
                boolean extra=minutes(a.getRemainingOvertimeWork(),project)>0 || !a.getRawTimephasedRemainingOvertimeWork().isEmpty();
                // This is an owned, disposable projection. The original binary is never rewritten.
                for(AssignmentField field:AssignmentField.values())
                    if(field.name().startsWith("RAW_TIMEPHASED_")) a.set(field,Collections.emptyList());
                if(observed) {++actual;continue;}
                if(extra) {++overtime;continue;}
                if(source.isEmpty()) {if(a.getWorkContour()==WorkContour.CONTOURED)++missing;continue;}
                if(source.size()>1024 || rows+source.size()>100000) throw new IllegalArgumentException("CURVE_ROW_LIMIT");
                Task task=a.getTask();
                if(task==null||task.getSummary()) throw new IllegalArgumentException("CURVE_TASK_REFERENCE");
                LocalDateTime origin=minute(task.getStart()),last=null;
                List<TimephasedWork> output=new ArrayList<>();
                List<Double> fractions=new ArrayList<>();
                double exactSum=0;
                long sum=0;
                for(TimephasedWork row:source) {
                    LocalDateTime begin=minute(row.getStart()),end=minute(row.getFinish());
                    if(!begin.equals(row.getStart()))++boundaries;
                    if(!end.equals(row.getFinish()))++boundaries;
                    if(begin.isBefore(origin)||!end.isAfter(begin)||(last!=null&&begin.isBefore(last)))
                        throw new IllegalArgumentException("CURVE_TIME_ORDER");
                    if(row.getTotalAmount()==null && minutes(row.getAmountPerHour(),project)>0)
                        throw new IllegalArgumentException("CURVE_MISSING_AMOUNT");
                    BigDecimal decimal=BigDecimal.valueOf(minutes(row.getTotalAmount(),project)).movePointRight(2).setScale(6,RoundingMode.HALF_UP);
                    double exact=decimal.doubleValue();
                    long amount=decimal.setScale(0,RoundingMode.FLOOR).longValueExact();exactSum+=exact;sum+=amount;
                    fractions.add(decimal.subtract(BigDecimal.valueOf(amount)).doubleValue());
                    if(Math.abs(exact-Math.rint(exact))>0.000001)++work;
                    TimephasedWork value=new TimephasedWork();value.setStart(begin);value.setFinish(end);
                    value.setAmountPerHour(row.getAmountPerHour());
                    value.setTotalAmount(Duration.getInstance(amount/100.0,TimeUnit.MINUTES));
                    output.add(value);last=end;
                }
                long total=Math.round(minutes(a.getWork(),project)*100);
                if(Math.abs(exactSum-minutes(a.getWork(),project)*100)>0.00001+source.size()*0.000001 || total-sum<0 || total-sum>source.size())
                    throw new IllegalArgumentException("CURVE_SOURCE_WORK_MISMATCH: "+a.getUniqueID());
                List<Integer> residual=new ArrayList<>();for(int i=0;i<output.size();++i)residual.add(i);
                residual.sort(Comparator.<Integer>comparingDouble(fractions::get).reversed());
                for(int i=0;i<total-sum;++i){TimephasedWork row=output.get(residual.get(i));row.setTotalAmount(Duration.getInstance((Math.round(minutes(row.getTotalAmount(),project)*100)+1)/100.0,TimeUnit.MINUTES));}
                a.set(AssignmentField.RAW_TIMEPHASED_REMAINING_REGULAR_WORK,output);
                a.setWork(Duration.getInstance(total/100.0,TimeUnit.MINUTES));
                if(!origin.equals(a.getStart()))++origins;
                a.setStart(origin);
                if(!finishes.containsKey(task)||last.isAfter(finishes.get(task)))finishes.put(task,last);
                ++assignments;rows+=output.size();
            }
            for(Task task:project.getTasks())
                if(task.getResourceAssignments().isEmpty() && task.getPercentageComplete()!=null && task.getPercentageComplete().doubleValue()!=0){
                    task.setPercentageComplete(0);++progressPlaceholders; // Suppress MSPDIWriter's synthesized progress-only assignment.
                }
            for(var entry:finishes.entrySet()) {
                Task task=entry.getKey();
                if(task.getEffectiveCalendar()==null)throw new IllegalArgumentException("CURVE_CALENDAR_REQUIRED");
                double original=minutes(task.getDuration(),project);
                double window=minutes(task.getEffectiveCalendar().getWork(minute(task.getStart()),entry.getValue(),TimeUnit.MINUTES),project);
                long projected=(long)Math.ceil(Math.max(original,window)-0.000001);
                if(projected>5000000) throw new IllegalArgumentException("CURVE_DURATION_LIMIT");
                if(Math.abs(original-projected)>0.000001)++durations;
                task.setDuration(Duration.getInstance(projected,TimeUnit.MINUTES));
            }
        }
        String json() {
            return "{\"policy\":\"source-regular-minute-v1\",\"assignments\":"+assignments+",\"rows\":"+rows+
                ",\"excludedActual\":"+actual+",\"excludedOvertime\":"+overtime+",\"missingManual\":"+missing+
                ",\"roundedBoundaries\":"+boundaries+",\"projectedDurations\":"+durations+
                ",\"rebasedOrigins\":"+origins+",\"roundedWorkRows\":"+work+",\"progressPlaceholders\":"+progressPlaceholders+"}";
        }
    }
    public static void main(String[] args) {
        try {
            if (args.length != 0) throw new IllegalArgumentException("ARGUMENTS");
            byte[] bytes = System.in.readNBytes(INPUT_LIMIT + 1);
            if (bytes.length > INPUT_LIMIT) throw new IllegalArgumentException("INPUT_LIMIT");
            String format;
            ProjectFile project;
            try (POIFSFileSystem fs = new POIFSFileSystem(new ByteArrayInputStream(bytes))) {
                format = MPPReader.getFileFormat(fs);
                if (format == null || !Set.of("MSProject.MPP8", "MSProject.MPP9", "MSProject.MPP12", "MSProject.MPP14").contains(format))
                    throw new IllegalArgumentException("UNSUPPORTED_MPP_FORMAT: " + format);
                MPPReader reader = new MPPReader();
                reader.setRespectPasswordProtection(true);
                reader.setReadPresentationData(false);
                project = reader.read(fs);
            }
            if (!project.getIgnoredErrors().isEmpty()) throw new IllegalArgumentException("READER_IGNORED_ERRORS");
            if (project.getTasks().size() > 10001 || project.getResources().size() > 10001 ||
                project.getResourceAssignments().size() > 10000 || project.getCalendars().size() > 10000)
                throw new IllegalArgumentException("ROW_LIMIT");
            var p = project.getProjectProperties();
            int sourceAssignments = project.getResourceAssignments().size();
            int unassigned = 0;
            for (ResourceAssignment assignment : new ArrayList<>(project.getResourceAssignments())) {
                Integer id = assignment.getResourceUniqueID();
                Resource resource = assignment.getResource();
                if ((id == null || id == -65535 || id == -1 || id == 0) &&
                    (resource == null || resource.getName() == null || resource.getName().isBlank())) {
                    project.getResourceAssignments().remove(assignment);
                    ++unassigned;
                }
            }
            String sourceName = p.getName();
            if (sourceName == null || sourceName.isBlank()) p.setName("MPP 계획");
            var curves=new CurveProjection();
            curves.prepare(project);
            var output = new BoundedOutput();
            var writer = new MSPDIWriter();
            writer.setWriteTimephasedData(true);
            writer.setGenerateMissingTimephasedData(false);
            writer.write(project, output);
            String metadata = "{\"protocol\":2,\"engine\":\"MPXJ\",\"engineVersion\":\"16.10.0\",\"format\":" + quote(format)
                + ",\"sourceName\":" + quote(sourceName) + ",\"sourceCurrency\":" + quote(p.getCurrencyCode())
                + ",\"tasks\":" + project.getTasks().size() + ",\"resources\":" + project.getResources().size()
                + ",\"assignments\":" + sourceAssignments + ",\"calendars\":" + project.getCalendars().size()
                + ",\"unassignedAssignments\":" + unassigned + ",\"planningCurves\":" + curves.json()+"}";
            System.out.write(("PPM-MPP/2\n" + metadata + "\n").getBytes(StandardCharsets.UTF_8));
            output.writeTo(System.out);
            System.out.flush();
        } catch (Throwable error) {
            String message = error.getMessage();
            if (message == null) message = error.getClass().getSimpleName();
            System.err.println("MPP_READER_ERROR: " + message.substring(0, Math.min(message.length(), 2048)));
            System.exit(2);
        }
    }
}
