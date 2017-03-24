; ModuleID = 'cabac.ll'
source_filename = "/Users/francisvm/llvm-test/test-suite/MultiSource/Applications/JM/ldecod/cabac.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-ios10.3.0"

%struct.DecodingEnvironment = type { i32, i32, i32, i32, i32, i8*, i32* }
%struct.BiContextType = type { i16, i8 }
%struct.syntaxelement = type { i32, i32, i32, i32, i32, i32, i32, i32, void (i32, i32, i32*, i32*)*, void (%struct.syntaxelement*, %struct.img_par*, %struct.DecodingEnvironment*)* }
%struct.img_par = type { i32, i32, i32, i32, i32*, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [16 x [16 x i16]], [6 x [32 x i32]], [16 x [16 x i32]], [4 x [12 x [4 x [4 x i32]]]], [16 x i32], i8**, i32*, i32***, i32**, i32, i32, i32, i32, %struct.Slice*, %struct.macroblock*, i32, i32, i32, i32, i32, i32, %struct.DecRefPicMarking_s*, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [3 x i32], i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32***, i32***, i32****, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, [3 x [2 x i32]], [3 x [2 x i32]], i32, i32, i64, i64, %struct.timeb, %struct.timeb, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }
%struct.Slice = type { i32, i32, i32, i32, i32, i32, i32, i32, i32, %struct.datapartition*, %struct.MotionInfoContexts*, %struct.TextureInfoContexts*, i32, i32*, i32*, i32*, i32, i32*, i32*, i32*, i32 (%struct.img_par*, %struct.inp_par*)*, i32, i32, i32, i32 }
%struct.datapartition = type { %struct.Bitstream*, %struct.DecodingEnvironment, i32 (%struct.syntaxelement*, %struct.img_par*, %struct.datapartition*)* }
%struct.Bitstream = type { i32, i32, i32, i32, i8*, i32 }
%struct.MotionInfoContexts = type { [4 x [11 x %struct.BiContextType]], [2 x [9 x %struct.BiContextType]], [2 x [10 x %struct.BiContextType]], [2 x [6 x %struct.BiContextType]], [4 x %struct.BiContextType], [4 x %struct.BiContextType], [3 x %struct.BiContextType] }
%struct.TextureInfoContexts = type { [2 x %struct.BiContextType], [4 x %struct.BiContextType], [3 x [4 x %struct.BiContextType]], [10 x [4 x %struct.BiContextType]], [10 x [15 x %struct.BiContextType]], [10 x [15 x %struct.BiContextType]], [10 x [5 x %struct.BiContextType]], [10 x [5 x %struct.BiContextType]], [10 x [15 x %struct.BiContextType]], [10 x [15 x %struct.BiContextType]] }
%struct.inp_par = type { [1000 x i8], [1000 x i8], [1000 x i8], i32, i32, i32, i32, i32, i32, i32, i32 }
%struct.macroblock = type { i32, [2 x i32], i32, i32, %struct.macroblock*, %struct.macroblock*, i32, [2 x [4 x [4 x [2 x i32]]]], i32, i64, i64, i32, i32, [4 x i8], [4 x i8], i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }
%struct.DecRefPicMarking_s = type { i32, i32, i32, i32, i32, %struct.DecRefPicMarking_s* }
%struct.timeb = type { i64, i16, i16, i16 }

declare i32 @biari_decode_symbol(%struct.DecodingEnvironment*, %struct.BiContextType*) local_unnamed_addr #0

; Function Attrs: nounwind ssp
define void @readB8_typeInfo_CABAC(%struct.syntaxelement* nocapture, %struct.img_par* nocapture readonly, %struct.DecodingEnvironment*) local_unnamed_addr #1 {
  %4 = getelementptr inbounds %struct.img_par, %struct.img_par* %1, i64 0, i32 10
  %5 = load i32, i32* %4, align 4, !tbaa !2
  %6 = icmp eq i32 %5, 1
  %7 = getelementptr inbounds %struct.img_par, %struct.img_par* %1, i64 0, i32 38
  %8 = load %struct.Slice*, %struct.Slice** %7, align 8, !tbaa !11
  %9 = getelementptr inbounds %struct.Slice, %struct.Slice* %8, i64 0, i32 10
  %10 = load %struct.MotionInfoContexts*, %struct.MotionInfoContexts** %9, align 8, !tbaa !12
  br i1 %6, label %24, label %11

; <label>:11:                                     ; preds = %3
  %12 = getelementptr inbounds %struct.MotionInfoContexts, %struct.MotionInfoContexts* %10, i64 0, i32 1, i64 0, i64 1
  %13 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %12) #2
  %14 = icmp eq i32 %13, 0
  br i1 %14, label %15, label %64

; <label>:15:                                     ; preds = %11
  %16 = getelementptr inbounds %struct.MotionInfoContexts, %struct.MotionInfoContexts* %10, i64 0, i32 1, i64 0, i64 3
  %17 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %16) #2
  %18 = icmp eq i32 %17, 0
  br i1 %18, label %64, label %19

; <label>:19:                                     ; preds = %15
  %20 = getelementptr inbounds %struct.MotionInfoContexts, %struct.MotionInfoContexts* %10, i64 0, i32 1, i64 0, i64 4
  %21 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %20) #2
  %22 = icmp eq i32 %21, 0
  %23 = select i1 %22, i32 3, i32 2
  br label %64

; <label>:24:                                     ; preds = %3
  %25 = getelementptr inbounds %struct.MotionInfoContexts, %struct.MotionInfoContexts* %10, i64 0, i32 1, i64 1, i64 0
  %26 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %25) #2
  %27 = icmp eq i32 %26, 0
  br i1 %27, label %64, label %28

; <label>:28:                                     ; preds = %24
  %29 = getelementptr inbounds %struct.MotionInfoContexts, %struct.MotionInfoContexts* %10, i64 0, i32 1, i64 1, i64 1
  %30 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %29) #2
  %31 = icmp eq i32 %30, 0
  br i1 %31, label %56, label %32

; <label>:32:                                     ; preds = %28
  %33 = getelementptr inbounds %struct.MotionInfoContexts, %struct.MotionInfoContexts* %10, i64 0, i32 1, i64 1, i64 2
  %34 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %33) #2
  %35 = icmp eq i32 %34, 0
  %36 = getelementptr inbounds %struct.MotionInfoContexts, %struct.MotionInfoContexts* %10, i64 0, i32 1, i64 1, i64 3
  %37 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %36) #2
  %38 = icmp eq i32 %37, 0
  br i1 %35, label %50, label %39

; <label>:39:                                     ; preds = %32
  %40 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %36) #2
  %41 = icmp eq i32 %40, 0
  br i1 %38, label %44, label %42

; <label>:42:                                     ; preds = %39
  %43 = select i1 %41, i32 10, i32 11
  br label %61

; <label>:44:                                     ; preds = %39
  %45 = select i1 %41, i32 6, i32 8
  %46 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %36) #2
  %47 = icmp ne i32 %46, 0
  %48 = zext i1 %47 to i32
  %49 = or i32 %45, %48
  br label %61

; <label>:50:                                     ; preds = %32
  %51 = select i1 %38, i32 2, i32 4
  %52 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %36) #2
  %53 = icmp ne i32 %52, 0
  %54 = zext i1 %53 to i32
  %55 = or i32 %51, %54
  br label %61

; <label>:56:                                     ; preds = %28
  %57 = getelementptr inbounds %struct.MotionInfoContexts, %struct.MotionInfoContexts* %10, i64 0, i32 1, i64 1, i64 3
  %58 = tail call i32 @biari_decode_symbol(%struct.DecodingEnvironment* %2, %struct.BiContextType* %57) #2
  %59 = icmp ne i32 %58, 0
  %60 = zext i1 %59 to i32
  br label %61

; <label>:61:                                     ; preds = %56, %50, %44, %42
  %62 = phi i32 [ %43, %42 ], [ %60, %56 ], [ %49, %44 ], [ %55, %50 ]
  %63 = add nsw i32 %62, 1
  br label %64

; <label>:64:                                     ; preds = %61, %24, %19, %15, %11
  %65 = phi i32 [ %63, %61 ], [ 0, %11 ], [ %23, %19 ], [ 1, %15 ], [ 0, %24 ]
  %66 = getelementptr inbounds %struct.syntaxelement, %struct.syntaxelement* %0, i64 0, i32 1
  store i32 %65, i32* %66, align 4, !tbaa !14
  ret void
}

attributes #0 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="cyclone" "target-features"="+crypto,+neon,+zcm,+zcz" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind ssp "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="cyclone" "target-features"="+crypto,+neon,+zcm,+zcz" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"PIC Level", i32 2}
!1 = !{!"clang version 5.0.0 (http://llvm.org/git/clang 3915a28878389759607b4562eba548dedf216298) (http://llvm.org/git/llvm 7ba46b38a60f5a2c6094d0bc9ce76575cede85ef)"}
!2 = !{!3, !4, i64 44}
!3 = !{!"img_par", !4, i64 0, !4, i64 4, !4, i64 8, !4, i64 12, !7, i64 16, !4, i64 24, !4, i64 28, !4, i64 32, !4, i64 36, !4, i64 40, !4, i64 44, !4, i64 48, !4, i64 52, !4, i64 56, !4, i64 60, !4, i64 64, !4, i64 68, !4, i64 72, !4, i64 76, !4, i64 80, !4, i64 84, !4, i64 88, !4, i64 92, !4, i64 96, !4, i64 100, !5, i64 104, !5, i64 616, !5, i64 1384, !5, i64 2408, !5, i64 5480, !7, i64 5544, !7, i64 5552, !7, i64 5560, !7, i64 5568, !4, i64 5576, !4, i64 5580, !4, i64 5584, !4, i64 5588, !7, i64 5592, !7, i64 5600, !4, i64 5608, !4, i64 5612, !4, i64 5616, !4, i64 5620, !4, i64 5624, !4, i64 5628, !7, i64 5632, !4, i64 5640, !4, i64 5644, !4, i64 5648, !4, i64 5652, !4, i64 5656, !4, i64 5660, !4, i64 5664, !4, i64 5668, !4, i64 5672, !4, i64 5676, !4, i64 5680, !4, i64 5684, !4, i64 5688, !4, i64 5692, !5, i64 5696, !4, i64 5708, !4, i64 5712, !4, i64 5716, !4, i64 5720, !4, i64 5724, !4, i64 5728, !4, i64 5732, !4, i64 5736, !4, i64 5740, !4, i64 5744, !4, i64 5748, !4, i64 5752, !4, i64 5756, !4, i64 5760, !4, i64 5764, !7, i64 5768, !7, i64 5776, !7, i64 5784, !4, i64 5792, !4, i64 5796, !4, i64 5800, !4, i64 5804, !4, i64 5808, !4, i64 5812, !4, i64 5816, !4, i64 5820, !4, i64 5824, !4, i64 5828, !4, i64 5832, !4, i64 5836, !4, i64 5840, !4, i64 5844, !4, i64 5848, !4, i64 5852, !4, i64 5856, !4, i64 5860, !4, i64 5864, !4, i64 5868, !4, i64 5872, !4, i64 5876, !4, i64 5880, !4, i64 5884, !4, i64 5888, !4, i64 5892, !4, i64 5896, !4, i64 5900, !4, i64 5904, !4, i64 5908, !4, i64 5912, !4, i64 5916, !4, i64 5920, !4, i64 5924, !4, i64 5928, !4, i64 5932, !4, i64 5936, !4, i64 5940, !4, i64 5944, !5, i64 5948, !5, i64 5972, !4, i64 5996, !4, i64 6000, !8, i64 6008, !8, i64 6016, !9, i64 6024, !9, i64 6040, !4, i64 6056, !4, i64 6060, !4, i64 6064, !4, i64 6068, !4, i64 6072, !4, i64 6076, !4, i64 6080, !4, i64 6084, !4, i64 6088, !4, i64 6092, !4, i64 6096, !4, i64 6100, !4, i64 6104}
!4 = !{!"int", !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C/C++ TBAA"}
!7 = !{!"any pointer", !5, i64 0}
!8 = !{!"long", !5, i64 0}
!9 = !{!"timeb", !8, i64 0, !10, i64 8, !10, i64 10, !10, i64 12}
!10 = !{!"short", !5, i64 0}
!11 = !{!3, !7, i64 5592}
!12 = !{!13, !7, i64 48}
!13 = !{!"", !4, i64 0, !4, i64 4, !4, i64 8, !4, i64 12, !5, i64 16, !4, i64 20, !4, i64 24, !4, i64 28, !4, i64 32, !7, i64 40, !7, i64 48, !7, i64 56, !4, i64 64, !7, i64 72, !7, i64 80, !7, i64 88, !4, i64 96, !7, i64 104, !7, i64 112, !7, i64 120, !7, i64 128, !4, i64 136, !4, i64 140, !4, i64 144, !4, i64 148}
!14 = !{!15, !4, i64 4}
!15 = !{!"syntaxelement", !4, i64 0, !4, i64 4, !4, i64 8, !4, i64 12, !4, i64 16, !4, i64 20, !4, i64 24, !4, i64 28, !7, i64 32, !7, i64 40}
