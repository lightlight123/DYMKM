; ModuleID = 'branch.ll'
source_filename = "branch.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [36 x i8] c"333This is a direct function call.\0A\00", align 1
@.str.1 = private unnamed_addr constant [39 x i8] c"444This is an indirect function call.\0A\00", align 1
@.str.2 = private unnamed_addr constant [33 x i8] c"Direct jump: value is positive.\0A\00", align 1
@.str.3 = private unnamed_addr constant [37 x i8] c"Direct jump: value is not positive.\0A\00", align 1
@.str.4 = private unnamed_addr constant [34 x i8] c"Press Enter to start the flow...\0A\00", align 1
@.str.5 = private unnamed_addr constant [36 x i8] c"111Direct jump: value is positive.\0A\00", align 1
@__const.main.jump_table = private unnamed_addr constant [2 x ptr] [ptr blockaddress(@main, %28), ptr blockaddress(@main, %30)], align 16
@.str.6 = private unnamed_addr constant [16 x i8] c"Jumping to: %p\0A\00", align 1
@.str.7 = private unnamed_addr constant [37 x i8] c"222Indirect jump: jumped to label1.\0A\00", align 1
@.str.8 = private unnamed_addr constant [34 x i8] c"Indirect jump: jumped to label2.\0A\00", align 1
@.str.9 = private unnamed_addr constant [31 x i8] c"Indirect function call to: %p\0A\00", align 1
@.str.10 = private unnamed_addr constant [19 x i8] c"555The sum is: %d\0A\00", align 1
@.str.11 = private unnamed_addr constant [24 x i8] c"Press Enter to exit...\0A\00", align 1
@0 = private unnamed_addr constant [20 x i8] c"direct_function_bb0\00", align 1
@1 = private unnamed_addr constant [42 x i8] c"{\22ID_source\22:\22%s\22,\22addrto_offset\22:0x%lx}\0A\00", align 1
@2 = private unnamed_addr constant [22 x i8] c"indirect_function_bb0\00", align 1
@3 = private unnamed_addr constant [42 x i8] c"{\22ID_source\22:\22%s\22,\22addrto_offset\22:0x%lx}\0A\00", align 1
@4 = private unnamed_addr constant [21 x i8] c"conditional_jump_bb3\00", align 1
@5 = private unnamed_addr constant [42 x i8] c"{\22ID_source\22:\22%s\22,\22addrto_offset\22:0x%lx}\0A\00", align 1
@6 = private unnamed_addr constant [16 x i8] c"add_numbers_bb0\00", align 1
@7 = private unnamed_addr constant [42 x i8] c"{\22ID_source\22:\22%s\22,\22addrto_offset\22:0x%lx}\0A\00", align 1
@8 = private unnamed_addr constant [9 x i8] c"main_bb6\00", align 1
@9 = private unnamed_addr constant [42 x i8] c"{\22ID_source\22:\22%s\22,\22addrto_offset\22:0x%lx}\0A\00", align 1
@10 = private unnamed_addr constant [9 x i8] c"main_bb6\00", align 1
@11 = private unnamed_addr constant [42 x i8] c"{\22ID_source\22:\22%s\22,\22addrto_offset\22:0x%lx}\0A\00", align 1
@12 = private unnamed_addr constant [9 x i8] c"main_bb7\00", align 1
@13 = private unnamed_addr constant [42 x i8] c"{\22ID_source\22:\22%s\22,\22addrto_offset\22:0x%lx}\0A\00", align 1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @direct_function() #0 {
  %1 = call i32 (ptr, ...) @printf(ptr noundef @.str)
  %2 = call ptr @llvm.returnaddress(i32 0)
  %3 = ptrtoint ptr %2 to i64
  %4 = sub i64 %3, ptrtoint (ptr @direct_function to i64)
  %5 = call i32 (ptr, ...) @printf(ptr @1, ptr @0, i64 %4)
  ret void
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @indirect_function() #0 {
  %1 = call i32 (ptr, ...) @printf(ptr noundef @.str.1)
  %2 = call ptr @llvm.returnaddress(i32 0)
  %3 = ptrtoint ptr %2 to i64
  %4 = sub i64 %3, ptrtoint (ptr @indirect_function to i64)
  %5 = call i32 (ptr, ...) @printf(ptr @3, ptr @2, i64 %4)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @conditional_jump(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = icmp sgt i32 %3, 0
  br i1 %4, label %5, label %7

5:                                                ; preds = %1
  %6 = call i32 (ptr, ...) @printf(ptr noundef @.str.2)
  br label %9

7:                                                ; preds = %1
  %8 = call i32 (ptr, ...) @printf(ptr noundef @.str.3)
  br label %9

9:                                                ; preds = %7, %5
  %10 = call ptr @llvm.returnaddress(i32 0)
  %11 = ptrtoint ptr %10 to i64
  %12 = sub i64 %11, ptrtoint (ptr @conditional_jump to i64)
  %13 = call i32 (ptr, ...) @printf(ptr @5, ptr @4, i64 %12)
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @add_numbers(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  store i32 %1, ptr %4, align 4
  %5 = load i32, ptr %3, align 4
  %6 = load i32, ptr %4, align 4
  %7 = add nsw i32 %5, %6
  %8 = call ptr @llvm.returnaddress(i32 0)
  %9 = ptrtoint ptr %8 to i64
  %10 = sub i64 %9, ptrtoint (ptr @add_numbers to i64)
  %11 = call i32 (ptr, ...) @printf(ptr @7, ptr @6, i64 %10)
  ret i32 %7
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i8, align 1
  %3 = alloca i32, align 4
  %4 = alloca [2 x ptr], align 16
  %5 = alloca ptr, align 8
  %6 = alloca ptr, align 8
  %7 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  %8 = call i32 (ptr, ...) @printf(ptr noundef @.str.4)
  %9 = call i32 @getchar()
  store i32 1, ptr %3, align 4
  %10 = load i32, ptr %3, align 4
  %11 = icmp sgt i32 %10, 0
  br i1 %11, label %12, label %14

12:                                               ; preds = %0
  %13 = call i32 (ptr, ...) @printf(ptr noundef @.str.5)
  br label %16

14:                                               ; preds = %0
  %15 = call i32 (ptr, ...) @printf(ptr noundef @.str.3)
  br label %16

16:                                               ; preds = %14, %12
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %4, ptr align 16 @__const.main.jump_table, i64 16, i1 false)
  %17 = load i32, ptr %3, align 4
  %18 = icmp sgt i32 %17, 0
  %19 = zext i1 %18 to i64
  %20 = select i1 %18, i32 0, i32 1
  %21 = sext i32 %20 to i64
  %22 = getelementptr inbounds [2 x ptr], ptr %4, i64 0, i64 %21
  %23 = load ptr, ptr %22, align 8
  store ptr %23, ptr %5, align 8
  %24 = load ptr, ptr %5, align 8
  %25 = call i32 (ptr, ...) @printf(ptr noundef @.str.6, ptr noundef %24)
  %26 = call i32 @getchar()
  %27 = load ptr, ptr %5, align 8
  br label %49

28:                                               ; preds = %49
  %29 = call i32 (ptr, ...) @printf(ptr noundef @.str.7)
  br label %32

30:                                               ; preds = %49
  %31 = call i32 (ptr, ...) @printf(ptr noundef @.str.8)
  br label %32

32:                                               ; preds = %30, %28
  %33 = call i32 @getchar()
  call void @direct_function()
  store ptr @indirect_function, ptr %6, align 8
  %34 = load ptr, ptr %6, align 8
  %35 = ptrtoint ptr %34 to i64
  %36 = sub i64 %35, ptrtoint (ptr @main to i64)
  %37 = call i32 (ptr, ...) @printf(ptr @9, ptr @8, i64 %36)
  call void (...) %34()
  %38 = load ptr, ptr %6, align 8
  %39 = call i32 (ptr, ...) @printf(ptr noundef @.str.9, ptr noundef %38)
  %40 = call i32 @add_numbers(i32 noundef 3, i32 noundef 5)
  store i32 %40, ptr %7, align 4
  %41 = load i32, ptr %7, align 4
  %42 = call i32 (ptr, ...) @printf(ptr noundef @.str.10, i32 noundef %41)
  %43 = call i32 (ptr, ...) @printf(ptr noundef @.str.11)
  %44 = call i32 @getchar()
  %45 = call ptr @llvm.returnaddress(i32 0)
  %46 = ptrtoint ptr %45 to i64
  %47 = sub i64 %46, ptrtoint (ptr @main to i64)
  %48 = call i32 (ptr, ...) @printf(ptr @11, ptr @10, i64 %47)
  ret i32 0

49:                                               ; preds = %16
  %50 = phi ptr [ %27, %16 ]
  %51 = ptrtoint ptr %50 to i64
  %52 = sub i64 %51, ptrtoint (ptr @main to i64)
  %53 = call i32 (ptr, ...) @printf(ptr @13, ptr @12, i64 %52)
  indirectbr ptr %50, [label %28, label %30]
}

declare i32 @getchar() #1

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i64, i1 immarg) #2

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare ptr @llvm.returnaddress(i32 immarg) #3

attributes #0 = { noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #3 = { nocallback nofree nosync nounwind willreturn memory(none) }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 18.1.8 (https://github.com/llvm/llvm-project.git 3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff)"}
